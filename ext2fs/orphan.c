/* Ext3-style Orphan Inode List implementation for ext2fs.
   When a file is unlinked (nlink=0) but still held open by a process,
   the inode is added to the orphan list (anchored at s_last_orphan in
   the superblock).  Each orphaned inode uses its i_dtime field as a
   "next" pointer in the singly-linked list on disk.  On mount, the list is
   traversed and each orphan is cleaned up (truncated and freed).
   At runtime, they are organized as a doubly linked list in memory.

   Copyright (C) 2026 Free Software Foundation, Inc.
   Written by Milos Nikic.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "ext2fs.h"
#include "journal.h"
#include "libdiskfs/diskfs.h"
#include <pthread.h>
#include <string.h>

/* Dedicated mutex to protect the Ext3 orphan linked list and s_last_orphan. */
static pthread_mutex_t orphan_lock = PTHREAD_MUTEX_INITIALIZER;

/* The in-memory head of the orphan doubly linked list. */
static struct node *ram_orphan_head = NULL;

/* Add inode NP to the orphan list. NP is locked by the caller. */
void
diskfs_orphan_add (struct node *np)
{
  ino_t inum = np->cache_id;
  struct ext2_inode *di;
  diskfs_transaction_t *txn;

  assert_backtrace (!diskfs_readonly);
  assert_backtrace (np->dn_stat.st_nlink == 0);

  /* The orphan list is exclusively an ext3/journaling feature. */
  if (!ext2_journal)
    return;

  if (diskfs_node_disknode (np)->on_orphan_list)
    return;

  /* SYNCHRONIZATION OVERVIEW:
     1. orphan_lock: Protects the in-memory doubly linked list (ram_orphan_head).
     2. global_lock: Protects the in-memory superblock modifications.
     3. Journal Transaction (txn): Guarantees that the superblock pointer and the
        inode pointer hit the physical disk as a single, atomic operation. */
  txn = journal_thread_transaction ();

  pthread_mutex_lock (&orphan_lock);

  if (diskfs_node_disknode (np)->on_orphan_list)
    {
      pthread_mutex_unlock (&orphan_lock);
      return;
    }

  /* write_node must see this before it next writes the inode.  While set,
     it leaves i_dtime alone and writes the rest of the inode as usual, so
     the block map on disk always matches the blocks NP still owns and
     recovery can truncate it.  libdiskfs must call diskfs_node_update as
     soon as this function returns. */
  diskfs_node_disknode (np)->on_orphan_list = 1;

  ext2_debug ("adding inode %lu to orphan list", (unsigned long) inum);

  di = dino_ref (inum);

  /* Reserve the inode block in the journal */
  journal_get_write_access (txn, boffs_block (bptr_offs (di)));

  pthread_spin_lock (&global_lock);
  di->i_dtime = sblock->s_last_orphan;
  sblock->s_last_orphan = htole32 (inum);
  sblock_dirty = 1;
  pthread_spin_unlock (&global_lock);

  di->i_links_count = 0;

  /* Let the journal know we are done editing. */
  journal_mark_dirty (txn, boffs_block (bptr_offs (di)));
  dino_deref (di);

  /* Maintain the in-memory doubly linked list for O(1) removals. */
  diskfs_node_disknode (np)->orphan_prev = NULL;
  diskfs_node_disknode (np)->orphan_next = ram_orphan_head;
  if (ram_orphan_head)
    diskfs_node_disknode (ram_orphan_head)->orphan_prev = np;
  ram_orphan_head = np;

  pthread_mutex_unlock (&orphan_lock);

  /* hyper.c handles the superblock's get_write_access -> memcpy -> mark_dirty! */
  diskfs_set_hypermetadata (0, 0);
}

/* Remove inode NP from the orphan list.  NP is locked by the caller. */
void
diskfs_orphan_del (struct node *np)
{
  ino_t inum = np->cache_id;
  diskfs_transaction_t *txn;
  int update_super = 0;

  if (!ext2_journal)
    return;

  if (!diskfs_node_disknode (np)->on_orphan_list)
    return;

  txn = journal_thread_transaction ();

  pthread_mutex_lock (&orphan_lock);

  if (!diskfs_node_disknode (np)->on_orphan_list)
    {
      pthread_mutex_unlock (&orphan_lock);
      return;
    }

  ext2_debug ("removing inode %lu from orphan list", (unsigned long) inum);

  struct ext2_inode *my_di = dino_ref (inum);
  __u32 my_next = le32toh (my_di->i_dtime);
  block_t blocknr = boffs_block (bptr_offs (my_di));

  /* This inode is leaving the list.  i_dtime becomes a normal deletion
     stamp in the caller's following write_node (mode is already 0).
     We let the journal know we are about to modify the associated block. */
  journal_get_write_access (txn, blocknr);
  my_di->i_dtime = 0;
  /* Done editing. */
  journal_mark_dirty (txn, blocknr);

  dino_deref (my_di);

  struct node *prev = diskfs_node_disknode (np)->orphan_prev;
  struct node *next = diskfs_node_disknode (np)->orphan_next;

  if (prev == NULL)
    {
      pthread_spin_lock (&global_lock);
      sblock->s_last_orphan = htole32 (my_next);
      sblock_dirty = 1;
      pthread_spin_unlock (&global_lock);

      update_super = 1;
      ram_orphan_head = next;
    }
  else
    {
      struct ext2_inode *prev_di = dino_ref (prev->cache_id);

      /* prev stays on the list; only its i_dtime changes. */
      journal_get_write_access (txn, boffs_block (bptr_offs (prev_di)));
      prev_di->i_dtime = htole32 (my_next);
      journal_mark_dirty (txn, boffs_block (bptr_offs (prev_di)));
      dino_deref (prev_di);

      diskfs_node_disknode (prev)->orphan_next = next;
    }

  if (next)
    diskfs_node_disknode (next)->orphan_prev = prev;

  diskfs_node_disknode (np)->on_orphan_list = 0;
  diskfs_node_disknode (np)->orphan_prev = NULL;
  diskfs_node_disknode (np)->orphan_next = NULL;

  pthread_mutex_unlock (&orphan_lock);

  /* Bundle the superblock modification into the transaction if needed */
  if (update_super)
    diskfs_set_hypermetadata (0, 0);
}

/* Recover (clean up) the orphan list at mount time.

   Only inodes with nlink == 0 are truncated and freed.  Entries with
   nlink != 0 (corruption, or ext3-style truncate orphans, which we do
   not support yet) are just taken off the list and otherwise left alone.

   The on-disk list is untrusted: each inode number is range-checked and
   the walk is bounded by the total inode count, so a cycle in the chain
   cannot hang the mount.  */
void
ext2_recover_orphan_list (void)
{
  ino_t inum;
  __u32 next_orphan;
  __u32 steps = 0;
  int count = 0;
  struct ext2_inode *di;
  struct node *np = NULL;
  diskfs_transaction_t *txn;
  error_t err;
  __u32 max_inodes = le32toh (sblock->s_inodes_count);

  next_orphan = le32toh (sblock->s_last_orphan);

  if (next_orphan == 0)
    return;

  if (diskfs_readonly)
    {
      ext2_warning ("orphan inodes on readonly fs; leaving for fsck");
      return;
    }

  /* No locks needed for the RAM list here; the filesystem is strictly
     single-threaded during mount.  */
  while (next_orphan != 0)
    {
      inum = next_orphan;

      /* Validate before touching the disk.  Counting steps (not
         recoveries) also bounds the nlink != 0 path below.  */
      if (inum < EXT2_FIRST_INO (sblock) || inum > max_inodes
	  || ++steps > max_inodes)
	{
	  ext2_warning ("corrupt or cyclic orphan list (inode %lu); "
			"aborting recovery", (unsigned long) inum);
	  /* Break the chain so we don't re-walk garbage on every mount.  */
	  pthread_spin_lock (&global_lock);
	  sblock->s_last_orphan = 0;
	  sblock_dirty = 1;
	  pthread_spin_unlock (&global_lock);
	  break;
	}

      /* Read the next pointer before lookup/deletion: orphan_del will
         scrub this inode's i_dtime.  */
      di = dino_ref (inum);
      next_orphan = le32toh (di->i_dtime);
      dino_deref (di);

      /* One handle per orphan, taken before the node lock like an RPC's:
	 orphan_del and the drop in diskfs_nput edit metadata in it.  */
      txn = diskfs_journal_start_transaction ();
      err = diskfs_cached_lookup (inum, &np);
      if (err || !np)
	{
	  diskfs_journal_stop_transaction (txn);
	  ext2_warning ("cannot look up orphan inode %lu: %s",
			(unsigned long) inum,
			err ? strerror (err) : "not found");
	  /* We can't safely unlink an inode we can't load.  Leave the
	     rest of the list, including s_last_orphan, for fsck.  */
	  break;
	}

      /* Mock the RAM list state so that diskfs_orphan_del can natively
         update the disk structure and s_last_orphan.  This inode is
         always the current head, so prev == NULL.  */
      diskfs_node_disknode (np)->on_orphan_list = 1;
      diskfs_node_disknode (np)->orphan_prev = NULL;
      diskfs_node_disknode (np)->orphan_next = NULL;
      ram_orphan_head = np;

      if (np->dn_stat.st_nlink != 0)
	{
	  /* Not a deleted file, so diskfs_nput would not drop it and
	     orphan_del would never run.  Unlink it explicitly; this
	     scrubs i_dtime, advances s_last_orphan and clears the head.  */
	  ext2_warning ("orphan inode %lu has nlink > 0; unlinking from list",
			(unsigned long) inum);
	  diskfs_orphan_del (np);
	  diskfs_nput (np);
	  diskfs_journal_stop_transaction (txn);
	  continue;
	}

      /* This drops the reference.  Since st_nlink == 0, libdiskfs will
         truncate the file and call diskfs_orphan_del (np), which
         advances s_last_orphan and clears the head.  */
      diskfs_nput (np);
      diskfs_journal_stop_transaction (txn);
      count++;
    }

  ram_orphan_head = NULL;

  if (count > 0)
    {
      /* Lets sync it, so that fsck doesn't try to clear the same orphans again. */
      diskfs_set_hypermetadata (1, 0);
      ext2_warning ("recovered %d orphan inode(s)", count);
    }
}

void
ext2_orphan_drop_ram_link (struct node *np)
{
  pthread_mutex_lock (&orphan_lock);

  if (diskfs_node_disknode (np)->on_orphan_list)
    {
      struct node *prev = diskfs_node_disknode (np)->orphan_prev;
      struct node *next = diskfs_node_disknode (np)->orphan_next;

      if (prev)
	diskfs_node_disknode (prev)->orphan_next = next;
      else
	ram_orphan_head = next;
      if (next)
	diskfs_node_disknode (next)->orphan_prev = prev;

      diskfs_node_disknode (np)->orphan_prev = NULL;
      diskfs_node_disknode (np)->orphan_next = NULL;
    }

  pthread_mutex_unlock (&orphan_lock);
}
