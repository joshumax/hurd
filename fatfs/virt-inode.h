/* virt-inode.h - Public interface for the virtual inode management routines.
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

#ifndef VIRT_INODE_H
#define VIRT_INODE_H

#include <errno.h>
#include <dirent.h>

/* Define struct vi_key to match your needs.  It is passed by copy,
   so don't make it too huge.  Equality is tested with memcpy, because
   C == operator doesn't work on structs.  */

struct vi_key
{
  ino_t dir_inode;
  int dir_offset;
};

typedef struct vi_key vi_key_t;

extern vi_key_t vi_zero_key;

typedef struct v_inode *inode_t;

/* Allocate a new inode number INODE for KEY and return it as well as
   the virtual inode V_INODE. Return 0 on success, otherwise an error
   value (ENOSPC).  */
error_t vi_new(vi_key_t key, ino_t *inode, inode_t *v_inode);

/* Get the key for virtual inode V_INODE. */
vi_key_t vi_key(inode_t v_inode);

/* Get the inode V_INODE belonging to inode number INODE.
   Returns 0 if this inode number is free.  */
inode_t vi_lookup(ino_t inode);

/* Get the inode number and virtual inode belonging to key KEY.
   Returns 0 on success and EINVAL if no inode is found for KEY and
   CREATE is false. Otherwise, if CREATE is true, allocate a new
   inode.  */
error_t vi_rlookup(vi_key_t key, ino_t *inode, inode_t *v_inode, int create);

/* Change the key of virtual inode V_INODE to KEY and return the old
   key. */
vi_key_t vi_change(inode_t v_inode, vi_key_t key);

/* Release virtual inode V_INODE, freeing the inode number.  Return
   the key.  */
vi_key_t vi_free(inode_t v_inode);

#endif
