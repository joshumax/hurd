/* Virtual Inode management routines
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

/* TODO: Improve NEW by keeping a bitmap of free inodes.
   TODO: Improve RLOOKUP by keeping an open hash for keys (need to change
   CHANGE and FREE, too).
   TODO: Improve FREE by keeping the highest inode in use and keep it
   up-to-date. When a table page can be freed, do so.  */

#include <stdlib.h>
#include <assert-backtrace.h>
#include <string.h>
#include <pthread.h>
#include "virt-inode.h"

/* Each virtual inode contains the UNIQUE key it belongs to,
   which must not be zero.  */

vi_key_t vi_zero_key = {0, 0};

struct v_inode
{
  vi_key_t key;
};

/* All inodes are stored in a table by their index number - 1.
   Decrementing by one is necessary because inode numbers start from 1,
   but our table is zero based.  */

#define LOG2_TABLE_PAGE_SIZE 10
#define TABLE_PAGE_SIZE (1 << LOG2_TABLE_PAGE_SIZE)

struct table_page
{
  struct table_page *next;

  struct v_inode vi[TABLE_PAGE_SIZE];
};

struct table_page *inode_table;

pthread_spinlock_t inode_table_lock = PTHREAD_SPINLOCK_INITIALIZER;

/* See vi_new and vi_rlookup.  */
error_t
_vi_new(vi_key_t key, ino_t *inode, inode_t *v_inode)
{
  struct table_page *table = inode_table;
  struct table_page *prev_table = 0;
  int page = 0;
  int offset = 0;

  while (table && memcmp(&vi_zero_key, &table->vi[offset].key, sizeof(vi_key_t)))
    {
      offset++;
      if (offset == TABLE_PAGE_SIZE)
	{
	  offset = 0;
	  page++;
	  prev_table = table;
	  table = table->next;
	}
    }

  if (table)
    {
      table->vi[offset].key = key;
      /* See above for rationale of increment. */
      *inode = (page << LOG2_TABLE_PAGE_SIZE) + offset + 1;
      *v_inode = &table->vi[offset];
    }
  else
    {
      struct table_page **pagep;

      if (prev_table)
	pagep = &prev_table->next;
      else
	pagep = &inode_table;
      *pagep = (struct table_page *) malloc (sizeof (struct table_page));
      if (!*pagep)
	{
	  return ENOSPC;
	}
      memset (*pagep, 0, sizeof (struct table_page));
      (*pagep)->vi[0].key = key;
      /* See above for rationale of increment. */
      *inode = (page << LOG2_TABLE_PAGE_SIZE) + 1;
      *v_inode = &(*pagep)->vi[0];
    }

  return 0;
}

/* Allocate a new inode number INODE for KEY and return it as well as
   the virtual inode V_INODE. Return 0 on success, otherwise an error
   value (ENOSPC).  */
error_t
vi_new(vi_key_t key, ino_t *inode, inode_t *v_inode)
{
  error_t err;

  assert_backtrace (memcmp(&vi_zero_key, &key, sizeof (vi_key_t)));

  pthread_spin_lock (&inode_table_lock);
  err = _vi_new(key, inode, v_inode);
  pthread_spin_unlock (&inode_table_lock);

  return err;
}

/* Get the key for virtual inode V_INODE.  */
vi_key_t
vi_key(inode_t v_inode)
{
  return v_inode->key;
}

/* Get the inode V_INODE belonging to inode number INODE.
   Returns 0 if this inode number is free.  */
inode_t
vi_lookup(ino_t inode)
{
  struct table_page *table = inode_table;
  /* See above for rationale of decrement. */
  int page = (inode - 1) >> LOG2_TABLE_PAGE_SIZE;
  int offset = (inode - 1) & (TABLE_PAGE_SIZE - 1);
  inode_t v_inode = 0;

  pthread_spin_lock (&inode_table_lock);

  while (table && page > 0)
    {
      page--;
      table = table->next;
    }

  if (table)
    v_inode = &table->vi[offset];

  pthread_spin_unlock (&inode_table_lock);

  return v_inode;
}

/* Get the inode number and virtual inode belonging to key KEY.
   Returns 0 on success and EINVAL if no inode is found for KEY and
   CREATE is false. Otherwise, if CREATE is true, allocate new inode.  */
error_t
vi_rlookup(vi_key_t key, ino_t *inode, inode_t *v_inode, int create)
{
  error_t err = 0;
  struct table_page *table = inode_table;
  int page = 0;
  int offset = 0;

  assert_backtrace (memcmp(&vi_zero_key, &key, sizeof (vi_key_t)));

  pthread_spin_lock (&inode_table_lock);

  while (table && memcmp(&table->vi[offset].key, &key, sizeof (vi_key_t)))
    {
      offset++;
      if (offset == TABLE_PAGE_SIZE)
	{
	  offset = 0;
	  page++;
	  table = table->next;
	}
    }

  if (table)
    {
      /* See above for rationale of increment. */
      *inode = (page << LOG2_TABLE_PAGE_SIZE) + offset + 1;
      *v_inode = &table->vi[offset];
    }
  else
    {
      if (create)
	err = _vi_new (key, inode, v_inode);
      else
	err = EINVAL;
    }

  pthread_spin_unlock (&inode_table_lock);

  return err;
}

/* Change the key of virtual inode V_INODE to KEY and return the old
   key. */
vi_key_t vi_change(inode_t v_inode, vi_key_t key)
{
  vi_key_t okey = v_inode->key;

  assert_backtrace (memcmp(&vi_zero_key, &key, sizeof (vi_key_t)));
  v_inode->key = key;
  return okey;
}

/* Release virtual inode V_INODE, freeing the inode number.  Return
   the key.  */
vi_key_t vi_free(inode_t v_inode)
{
  vi_key_t key;
  pthread_spin_lock (&inode_table_lock);
  key = v_inode->key;
  v_inode->key = vi_zero_key;
  pthread_spin_unlock (&inode_table_lock);
  return key;
}
