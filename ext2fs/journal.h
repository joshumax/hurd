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

/**
 * A block device filter layer for the VFS pager's write path.
 * Replaces raw store_write calls to safely intercept deadlock hazards.
 * When the pager attempts to write a block that is actively locked by a
 * running or committing transaction (rushing the transaction commit cycle),
 * this function redirects the payload into temporary storage (the Lifeboat
 * cache). This preserves the Write-Ahead Log (WAL) ordering and prevents
 * the VM pager from deadlocking the filesystem. Safe blocks are coalesced
 * and written normally to disk.
 */
error_t
journal_store_write (block_t start_block, size_t length, void *buf,
		     size_t *amount);

/**
 * A block device filter layer for the VFS pager's read path.
 * Passes the read through to the underlying physical disk, and then
 * transparently overlays any fresh data from the temporary Lifeboat cache.
 * This ensures that reads of blocks which were recently intercepted and
 * redirected to temporary storage (due to rushing the transaction cycle)
 * return the most up-to-date data, maintaining strict cache coherence
 * without blocking the pager.
 */
error_t
journal_store_read (block_t start_block, size_t length, void **buf,
		    size_t *read_amount);

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
