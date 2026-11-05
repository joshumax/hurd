/* JBD2 binary compliant journal driver for ext2

   Implements "Writeback" journaling mode:
     - Metadata (Inodes, Bitmaps, Superblock) is journaled and crash-consistent.
     - File Data is written directly to disk (not journaled) and lacks
       explicit ordering guarantees relative to the metadata commit.
   This provides the best performance but allows for "stale data" in
   recently allocated blocks after a crash.

   Copyright (C) 2026 Free Software Foundation, Inc.
   Written by Milos Nikic.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef _JOURNAL_H
#define _JOURNAL_H

#include <stdint.h>
#include <stdio.h>
#include <libdiskfs/diskfs.h>

#include "ext2fs.h"

#ifndef JOURNAL_DEBUG
#define JOURNAL_DEBUG 0		/* Set to enable (very chatty) debug messages. */
#endif

#if JOURNAL_DEBUG
#define JRNL_LOG_DEBUG(fmt, ...)                                             \
    do {                                                                     \
        fprintf(stderr, "[JRNL][DEBUG] " fmt "\n", ##__VA_ARGS__);           \
        fflush(stderr);                                                      \
    } while (0)
#else
#define JRNL_LOG_DEBUG(fmt, ...) do { } while (0)
#endif

/* Opaque handle for the journal object */
typedef struct journal journal_t;

/* Initialize the journal subsystem using the inode provided (usually Inode 8). */
journal_t *journal_create (struct node *journal_inode);

/* What journal_check finds in a journal's superblock on disk.  */
enum journal_state
{
  JOURNAL_USABLE,		/* Empty log, features this journal writes.  */
  JOURNAL_NEEDS_REPLAY,		/* The log holds transactions that were never
				   replayed, the journal was aborted, or the
				   superblock is unreadable.  e2fsck must run
				   first.  */
  JOURNAL_UNSUPPORTED		/* Empty log, but a feature this journal does
				   not write, such as 64-bit tags or
				   checksums.  */
};

/* Look at the superblock of the journal in JOURNAL_INODE on disk.
   JOURNAL_INODE is locked.  */
enum journal_state journal_check (struct node *journal_inode);

/**
 * A block device filter layer for the VFS pager's write path.
 * Replaces raw store_write calls to safely intercept deadlock hazards.
 * When the pager attempts to write a block that is actively locked by a
 * running or committing transaction (rushing the transaction commit cycle),
 * this function takes the payload into the block's journal shadow and
 * writes it home after the commit.  This preserves the Write-Ahead Log
 * (WAL) ordering and prevents the VM pager from deadlocking the
 * filesystem.  Safe blocks are coalesced and written normally to disk.
 * DISK_CACHE is true for writes of the disk cache, false for those of a
 * file pager.
 */
error_t
journal_store_write (block_t start_block, size_t length, void *buf,
		     size_t *amount, int disk_cache);

/**
 * A block device filter layer for the VFS pager's read path.
 * Passes the read through to the underlying physical disk, and returns
 * the journal shadow instead for blocks whose pager writes were taken in
 * before their transaction committed, so that such reads get the latest
 * data without blocking the pager.
 */
error_t
journal_store_read (block_t start_block, size_t length, void **buf,
		    size_t *read_amount);

/**
 * Same as journal_get_write_access, for a metadata block whose live bytes
 * are outside the disk cache, such as a directory block edited through
 * the directory's file pager.  The journal makes no copy of its own; the
 * caller supplies the edited image through journal_mark_dirty_external,
 * or gives the block up through journal_release_access.
 */
error_t journal_get_write_access_external (diskfs_transaction_t *txn,
					   block_t fs_blocknr);

/**
 * Hands the journal the edited image of FS_BLOCKNR, block_size bytes at
 * DATA.  DATA may be a mapping of the block.  Returns EIO when DATA
 * cannot be read.
 */
error_t journal_mark_dirty_external (diskfs_transaction_t *txn,
				     block_t fs_blocknr, const void *data);

/**
 * Gives up FS_BLOCKNR, reserved with journal_get_write_access_external,
 * when the transaction has no image of it yet.
 */
void journal_release_access (diskfs_transaction_t *txn, block_t fs_blocknr);

/**
 * Records a range of deleted blocks so they can be unpinned from older
 * checkpoint lists AFTER this transaction safely commits.  Returns 1 if
 * the range was recorded: the journal then hands it back with
 * ext2_release_busy_blocks once the commit is done.
 */
int journal_record_freed_blocks (diskfs_transaction_t *txn, block_t start, unsigned long count);

/**
 * Waits until the transaction committing now has handed its freed blocks
 * back to the allocator.  TXN is the caller's handle.  Returns 1 after
 * such a wait, 0 if there was nothing the caller can wait for.
 */
int journal_wait_freed_blocks (diskfs_transaction_t *txn);

/**
 * Marks the calling thread as running a pager callback (ON = 1) or done
 * with it (ON = 0).  A pager thread must never wait on the journal: an
 * RPC thread inside a transaction may be waiting on it for a page.  While
 * marked, diskfs_journal_start_transaction joins the transaction that a
 * commit is draining instead of waiting for its successor.
 */
void journal_thread_set_pager (int on);

#endif //_JOURNAL_H
