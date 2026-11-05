/* JBD2 binary compliant journal driver.

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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert-backtrace.h>
#include <endian.h>
#include <pthread.h>
#include <mach.h>

#include <hurd/sigpreempt.h>
#include <hurd/store.h>
#include <libdiskfs/diskfs.h>
#include "ext2fs.h"
#include "jbd2_format.h"
#include "journal.h"

/* Journal Tuning Params */

/**
 * We limit a single transaction to 1/4 of the total journal size.
 * This ensures we can pipeline:
 * [ Committing Txn ] + [ Running Txn ] + [ Buffer/Wrap Space ]
 */
#define JRNL_MAX_TRANS_RATIO   4

/**
 * The minimum size (in blocks) of a transaction.
 * Below this, the overhead of commit records outweighs the data throughput.
 * 256 blocks = 1MB (assuming 4k blocks).
 */
#define JRNL_MIN_BATCH_BLOCKS  256

/**
 * Reserve space for metadata overhead (Descriptor blocks + Commit block).
 * 32 blocks allows for ~8000 data blocks to be described (approx),
 * which is plenty of safety margin for the size limits above.
 */
#define JRNL_METADATA_OVERHEAD 32

/**
 * Ratio for estimating descriptor blocks.
 * We estimate 1 descriptor block for every 32 data blocks.
 * (Real capacity is ~250 tags/block, so 32 is a very conservative/safe estimate).
 */
#define JRNL_DESCRIPTOR_RATIO  32

/**
 * Safety margin during commit.
 * Reserves space for: 1 Commit Block + 1 Descriptor Block + 3 blocks slop
 * to handle alignment/wrapping edge cases without hitting the tail.
 */
#define JRNL_COMMIT_MARGIN     5

/**
 * Low Water Mark Ratio.
 * If free space drops below 1/8th of the total journal, we force a checkpoint.
 * This ensures the NEXT transaction has breathing room to start.
 */
#define JRNL_LOW_WATER_RATIO   8

/**
 * Background checkpoint ratios.
 * kjournald starts checkpointing once less than 1/2 of the journal is free,
 * and stops once 3/4 of it is free again.  The gap keeps it from running
 * on every wake, and lets the pager write hot blocks lazily in between.
 */
#define JRNL_BG_START_RATIO    2
#define JRNL_BG_STOP_RATIO     4

/**
 * Slab Allocator Pool Size.
 * Pre-allocates a contiguous chunk of memory for journal buffers
 * (512 * 4KB = 2MB).
 * This allows journal_get_write_access to be a zero-allocation operation for the
 * vast majority of workloads, falling back to dynamic allocation only under
 * extreme metadata pressure.
 */
#define JRNL_MAX_FREE_BUFFERS 512

/**
 * Initial capacity for the per-transaction Robin Hood hash map.
 * Must be a power of 2. We start small (32) so that tiny metadata updates
 * don't waste memory, but the map will dynamically double in size
 * as the transaction grows.
 */
#define JRNL_MAP_INIT_CAPACITY 32

#define JOURNAL_LOCK(j)  \
  do { \
    assert_backtrace ((j) != NULL); \
    pthread_mutex_lock(&(j)->j_state_lock); \
  } while (0)

#define JOURNAL_UNLOCK(j) \
  do { \
    assert_backtrace ((j) != NULL); \
    pthread_mutex_unlock(&(j)->j_state_lock); \
  } while (0)

#define JOURNAL_WAIT(cond, j) \
    pthread_cond_wait((cond), &(j)->j_state_lock)

#define IS_RUNNING_TID(j, tid) \
    ((j)->j_running_transaction && (j)->j_running_transaction->t_tid == (tid))

#define JRNL_INTERCEPT_CAPACITY 1024


/* The handle this thread currently holds, and how many starts are open
   on it.  One RPC is one thread, and its metadata updates must all land
   in one transaction.  The outermost start on a thread is the only one
   that looks at the journal; every nested start on the same thread
   returns this transaction.  Only the outermost handle is counted in
   t_active_threads, so t_active_threadscounts threads, and t_active_threads== 0 means no
   thread holds a handle. */
static __thread diskfs_transaction_t *journal_thread_txn;
static __thread unsigned int journal_thread_depth;

/* Set while this thread runs a pager callback.  A pager thread must never
   wait on the journal: an RPC thread inside a transaction may be waiting
   on this very thread for a page, and the commit waits on that RPC.  An
   outermost start on a pager thread therefore joins the committing
   transaction while it drains rather than waiting for the successor. */
static __thread int journal_thread_is_pager;

/* Set when a nested call on this thread asked for a synchronous commit.
   The RPC tail reads it through diskfs_journal_needs_sync and decides
   between commit and stop.  It is per thread so an RPC waits only for
   its own request; another thread's request on the shared transaction
   is that thread's business.  Cleared by the outermost start. */
static __thread int journal_thread_sync;

/* Allocated in journal_create.  journal_mark_dirty_external copies the
   caller's image here before it takes the journal lock.  */
static void *journal_external_scratch;
static pthread_mutex_t journal_external_scratch_lock =
  PTHREAD_MUTEX_INITIALIZER;

/* Allocated in journal_create.  The stop-time sweep copies a block of the
   disk cache here outside the journal lock, then into the shadow under
   it.  A separate lock from the one above: its holder only faults on the
   disk cache, whose pager threads never take it, while a sweep can run
   on a file pager thread, which must not wait for a file pager page-in
   that journal_mark_dirty_external is holding its lock across.  */
static void *journal_sweep_scratch;
static pthread_mutex_t journal_sweep_scratch_lock =
  PTHREAD_MUTEX_INITIALIZER;

/**
 * Holds one modified block (4KB) that needs to be written to the journal.
 */
typedef struct journal_buffer
{
  block_t jb_blocknr;		/* The physical block number on the filesystem */
  char jb_shadow_data[4096];	/* 4KB Copy of the data to be logged */
  struct journal_buffer *jb_next;	/* Linked list next pointer */
  uint8_t jb_is_written;	/* Has this buffer been rushed by the VM pager */
  uint8_t needs_copy;		/* Whether this buffer needs a new copy from
				   from the live Mach VM cache. Should be 1
				   when new. */
  uint8_t needs_flush;		/* A pager write of the block was taken into
				   the shadow instead of going home.  The
				   home copy is stale until the shadow is
				   written there after the commit; reads are
				   served from the shadow until then.  */
  uint8_t jb_sweeping;		/* The stop-time sweep is copying the block
				   outside the lock.  */
  uint8_t jb_sweep_raced;	/* A pager write filled the shadow while
				   the sweep was copying, so the sweep's
				   copy may be older and is dropped.  */
  uint8_t jb_is_flushing;	/* 1 if commit thread is actively flushing it. */
  uint8_t jb_escaped;		/* Starts with the JBD2 magic number: the
				   log gets a copy with it zeroed.  */
  uint8_t jb_external;		/* Reserved through
				   journal_get_write_access_external: a
				   directory block, whose live bytes are in
				   the directory's file pager.  The disk
				   cache's copy is stale and must never
				   reach the shadow.  */
  uint8_t jb_awaiting_data;	/* Reserved by
				   journal_get_write_access_external, and
				   the shadow has no image of the block's
				   current owner yet. */
  uint8_t jb_editing;		/* Reserved by
				   journal_get_write_access_external and not
				   yet marked or released: the page can hold
				   an edit newer than the shadow.  */
} journal_buffer_t;

/**
 * A single bucket for custom hash table.
 */
typedef struct
{
  block_t blocknr;		/* The key */
  uint32_t probe_dist;		/* Robin Hood distance from ideal slot */
  journal_buffer_t *jb;		/* The value. NULL means this bucket is empty. */
} journal_bucket_t;

typedef struct
{
  journal_bucket_t *buckets;
  size_t capacity;		/* Must be a power of 2 */
  size_t mask;			/* capacity - 1 */
  size_t size;			/* Number of active items */
} journal_block_map_t;

/* The state of a transaction in memory */
typedef enum
{
  T_RUNNING,			/* Accepting new handles/buffers */
  T_LOCKED,			/* Locked, no new handles, waiting for updates
				   to finish */
  T_FLUSHING,			/* Writing to the journal ring buffer */
  T_COMMITTED,			/* WAL Commit Record is on disk. Safe to write directly to main FS. */
  T_FINISHED,			/* Done, waiting to be checkpointed */
  T_ABORTED			/* Its commit failed: kept in memory, never
				   written home (journal_abort_locked) */
} transaction_state_t;

typedef struct journal_freed_extent
{
  block_t fe_start;
  unsigned long fe_count;
  struct journal_freed_extent *fe_next;
} journal_freed_extent_t;

/* The Transaction Object */
struct diskfs_transaction
{
  uint32_t t_tid;		/* Transaction ID (Sequence Number) */
  transaction_state_t t_state;

  /* The Log Position */
  uint32_t t_log_start;		/* Where this transaction starts in the ring */

  uint32_t t_active_threads;		/* Refcount: How many threads are in this transaction? */

  /* The Map: block_t -> journal_buffer_t (for O(1) lookups).
   * We use a per-transaction map rather than a global, journal-wide map.
   * While a global map would make asynchronous block notifications slightly
   * faster, it makes transaction cleanup significantly slower and more fragile.
   * Because the strict capacity geometry of our ring buffer and proactive
   * checkpointing strictly limit the number of concurrent transactions,
   * the O(N) iteration over active transactions during a block notification
   * is strictly bounded to a tiny number. Therefore, per-transaction maps
   * provide the best balance of fast lookups and instantaneous,
   * memory-safe cleanup. */
  journal_block_map_t t_buffer_map;
  struct diskfs_transaction *t_checkpoint_next;
  /*
   * How many buffers in the t_buffer_map have jb_is_written == 0.
   *
   * The transaction's space in the journal ring buffer cannot be safely
   * reclaimed (checkpointed) until this counter drops to exactly 0.
   */
  int t_outstanding_io;

  journal_freed_extent_t *t_freed_blocks;	/* Blocks deleted in this txn */
  /* Freed blocks that still have a copy in the log, written as revoke
     records at commit.  A set: see journal_map_count_value.  */
  journal_block_map_t t_revoke_map;
  uint8_t sync_needed;		/* IOU flag for synchronous commits */
};

/* The Simple Mapper (Virtual -> Physical) */
typedef struct journal_map
{
  block_t *phys_blocks;		/* The 64KB array we malloc'd */
  uint32_t total_blocks;	/* 16384 */
  struct node *inode;		/* Inode 8 (for keeping ref) */
} journal_map_t;

/* The Grand Abstraction */
typedef struct journal
{
  /* The Physics of it (The Map) */
  journal_map_t map;

  /* The Ring Buffer State (The Logic) */
  uint32_t j_head;		/* Where we are writing next */
  uint32_t j_tail;		/* The oldest live transaction (checkpoint) */
  uint32_t j_first;		/* First block of data (usually 1, after SB) */
  uint32_t j_last;		/* Last block of data */
  uint32_t j_free;		/* How many blocks left? */

  /* The Sequence Counter */
  uint32_t j_transaction_sequence;	/* Monotonic ID (e.g. 500, 501...) */
  void *j_sb_buffer;		/* Buffer holding the journal superblock */

  pthread_mutex_t j_state_lock;	/* Protects the pointers below */
  pthread_cond_t j_commit_wait;	/* Cond. var. while waiting for the tx to be ready. */
  pthread_cond_t j_flush_wait;	/* Cond. var for safely waiting on physical flushes */
  pthread_cond_t j_running_wait;	/* Cond. var for starts waiting while the
					   running transaction drains for commit */
  /* The Transactions */
  diskfs_transaction_t *j_running_transaction;	/* Currently filling */
  diskfs_transaction_t *j_committing_transaction;	/* Transaction that is
							   being committed. */
  diskfs_transaction_t *j_checkpoint_list;	/* Head (Oldest, defines j_tail) */
  diskfs_transaction_t *j_checkpoint_last;
  /* Transactions that left the checkpoint list, linked through
     t_checkpoint_next.  The journal superblock on disk can still put them
     in the log, so their log space and their maps are kept until a
     superblock with the new tail has been written.  */
  diskfs_transaction_t *j_retired_list;
  /* The tail the journal superblock on disk has, 0 for an empty log.
     It can be older than J_TAIL until journal_write_tail_locked writes
     the new one.  */
  uint32_t j_disk_tail;
  int j_tail_writing;		/* journal_write_tail_locked is at work.  */
  pthread_cond_t j_tail_written;	/* Signalled when it is done.  */

  /* Blocks with a copy in a live transaction (running, committing,
     checkpoint or retired), counted per transaction holding one.  A
     counter: see journal_map_count_value.  A freed block found here
     needs a revoke record.  When an insertion fails for lack of memory,
     j_logged_lost is set and every freed block is revoked from then on.  */
  journal_block_map_t j_logged;
  int j_logged_lost;

  uint32_t j_max_transaction_buffers;	/* Max size of a single transaction */
  uint32_t j_min_free;

  uint32_t j_last_committed_tid;	/* Transaction ID of the last committed txn. */
  uint32_t j_released_tid;	/* Last txn whose freed blocks went back
				   to the allocator. */
  /* Set when a commit fails (journal_abort_locked), and never cleared:
     what every commit from j_aborted_tid on returns.  The aborted
     transaction is kept, as the only copy of pages the pager has
     handed over.  */
  error_t j_errno;
  uint32_t j_aborted_tid;
  diskfs_transaction_t *j_aborted_transaction;
  pthread_cond_t j_commit_done;	/* Cond. var. while waiting for the tx to be committed. */

  /* The journal is stopping, set by diskfs_journal_shutdown under the
     lock and never cleared: no new handle starts, kjournald exits, no
     background commit, checkpoint or tail write runs, and the checkpoint
     list belongs to journal_quiesce_checkpoints.  A call made on a handle
     already open does not look at it: the stop's final commit waits for
     that handle, and carries what it reserves.  */
  int j_must_exit;
  pthread_t j_kjournald;	/* The journal thread, joined when the
				   journal stops.  */
  int j_kjournald_started;
  pthread_cond_t j_flusher_wakeup;	/* Cond. var for the kjournald sleep cycle */
  int j_commit_requested;	/* A stop asked kjournald to commit now. */

  int j_checkpointing;		/* A flusher owns the checkpoint list. */
  pthread_cond_t j_checkpoint_done;	/* Signalled when that flusher is done. */

  /* Pre-allocated buffers for zero-allocation commits */
  void *j_descriptor_buf;
  void *j_commit_buf;
  void *j_escape_buf;		/* The log copy of an escaped block.  */
  /* Pre-allocated buffers for (near) zero-allocation journal_get_write_access */
  journal_buffer_t *j_pool_memory;	/* The raw contiguous block */
  journal_buffer_t *j_free_buffers;	/* The linked list head */

} journal_t;

/* Specialized integer hash */
static inline uint32_t
journal_hash_block (block_t blocknr)
{
  uint64_t h = (uint64_t) blocknr;
  h ^= h >> 23;
  h *= 0x2127599bf4325c37ULL;
  h ^= h >> 47;
  return (uint32_t) h;
}

static error_t
journal_map_init (journal_block_map_t *map, size_t initial_cap)
{
  size_t cap =
    initial_cap <
    JRNL_MAP_INIT_CAPACITY ? JRNL_MAP_INIT_CAPACITY : initial_cap;

  size_t pow2 = 1;
  while (pow2 < cap)
    pow2 *= 2;

  map->buckets = calloc (pow2, sizeof (journal_bucket_t));
  if (!map->buckets)
    return ENOMEM;

  map->capacity = pow2;
  map->mask = pow2 - 1;
  map->size = 0;
  return 0;
}

static void
journal_map_destroy (journal_block_map_t *map)
{
  free (map->buckets);
  map->buckets = NULL;
  map->capacity = 0;
  map->size = 0;
}

/**
 * Doubles the capacity of the block map and rehashes all existing elements.
 * Returns 0 on success, or ENOMEM if allocation fails.
 */
static error_t
journal_map_resize (journal_block_map_t *map)
{
  size_t old_cap = map->capacity;
  journal_bucket_t *old_buckets = map->buckets;
  size_t new_cap = old_cap * 2;
  journal_bucket_t *new_buckets = calloc (new_cap, sizeof (journal_bucket_t));
  if (!new_buckets)
    return ENOMEM;

  size_t new_mask = new_cap - 1;

  for (size_t i = 0; i < old_cap; i++)
    {
      journal_bucket_t *old_b = &old_buckets[i];
      if (old_b->jb != NULL)
	{
	  block_t blocknr = old_b->blocknr;
	  journal_buffer_t *jb = old_b->jb;
	  uint32_t pos = journal_hash_block (blocknr) & new_mask;
	  uint32_t dist = 0;
	  for (;;)
	    {
	      journal_bucket_t *new_b = &new_buckets[pos];
	      if (new_b->jb == NULL)
		{
		  new_b->blocknr = blocknr;
		  new_b->probe_dist = dist;
		  new_b->jb = jb;
		  break;
		}
	      /* Robin Hood Swap: Displace items that are closer to their
	       * ideal hash position than our current item. */
	      if (new_b->probe_dist < dist)
		{
		  block_t tmp_block = new_b->blocknr;
		  uint32_t tmp_dist = new_b->probe_dist;
		  journal_buffer_t *tmp_jb = new_b->jb;

		  new_b->blocknr = blocknr;
		  new_b->probe_dist = dist;
		  new_b->jb = jb;

		  blocknr = tmp_block;
		  dist = tmp_dist;
		  jb = tmp_jb;
		}

	      pos = (pos + 1) & new_mask;
	      dist++;
	    }
	}
    }

  free (old_buckets);
  map->buckets = new_buckets;
  map->capacity = new_cap;
  map->mask = new_mask;
  /* map->size remains exactly the same! */

  return 0;
}

static error_t
journal_map_insert (journal_block_map_t *map, block_t blocknr,
		    journal_buffer_t *jb)
{
  if (map->size * 100 >= map->capacity * 85)
    {
      error_t err = journal_map_resize (map);
      if (err)
	return err;
    }

  uint32_t pos = journal_hash_block (blocknr) & map->mask;
  uint32_t dist = 0;

  for (;;)
    {
      journal_bucket_t *bucket = &map->buckets[pos];
      if (bucket->jb == NULL)
	{
	  bucket->blocknr = blocknr;
	  bucket->probe_dist = dist;
	  bucket->jb = jb;
	  map->size++;
	  return 0;
	}

      if (bucket->blocknr == blocknr)
	{
	  /* Callers look the block up first.  */
	  assert_backtrace (!"block inserted twice into a journal map");
	  return EEXIST;
	}

      /* Robin Hood Swap: If the current bucket is closer to its ideal
       * position than our new item is, we steal this slot and continue
       * finding a home for the displaced item. */
      if (bucket->probe_dist < dist)
	{
	  block_t tmp_block = bucket->blocknr;
	  uint32_t tmp_dist = bucket->probe_dist;
	  journal_buffer_t *tmp_jb = bucket->jb;

	  bucket->blocknr = blocknr;
	  bucket->probe_dist = dist;
	  bucket->jb = jb;

	  blocknr = tmp_block;
	  dist = tmp_dist;
	  jb = tmp_jb;
	}

      pos = (pos + 1) & map->mask;
      dist++;
    }
}

/* Returns the bucket holding BLOCKNR, or NULL.  */
static journal_bucket_t *
journal_map_find (journal_block_map_t *map, block_t blocknr)
{
  if (!map->buckets)
    return NULL;

  uint32_t pos = journal_hash_block (blocknr) & map->mask;
  uint32_t dist = 0;

  for (;;)
    {
      journal_bucket_t *bucket = &map->buckets[pos];

      if (bucket->jb == NULL)
	return NULL;
      if (bucket->blocknr == blocknr)
	return bucket;

      /* If we've probed further than the item sitting here, our target
       * cannot possibly be in this table. Stop searching. */
      if (dist > bucket->probe_dist)
	return NULL;

      pos = (pos + 1) & map->mask;
      dist++;
    }
}

static journal_buffer_t *
journal_map_lookup (journal_block_map_t *map, block_t blocknr)
{
  journal_bucket_t *bucket = journal_map_find (map, blocknr);

  return bucket ? bucket->jb : NULL;
}

/* Robin Hood deletion.  MUST be called with JOURNAL_LOCK held.  */
static journal_buffer_t *
journal_map_remove (journal_block_map_t *map, block_t blocknr)
{
  uint32_t pos, dist, next;
  journal_buffer_t *found;

  if (!map->buckets)
    return NULL;

  pos = journal_hash_block (blocknr) & map->mask;
  dist = 0;
  for (;;)
    {
      journal_bucket_t *bucket = &map->buckets[pos];

      if (bucket->jb == NULL)
	return NULL;
      if (bucket->blocknr == blocknr)
	break;
      if (dist > bucket->probe_dist)
	return NULL;

      pos = (pos + 1) & map->mask;
      dist++;
    }

  found = map->buckets[pos].jb;
  for (;;)
    {
      journal_bucket_t *nb;

      next = (pos + 1) & map->mask;
      nb = &map->buckets[next];
      if (nb->jb == NULL || nb->probe_dist == 0)
	{
	  map->buckets[pos].jb = NULL;
	  map->buckets[pos].blocknr = 0;
	  map->buckets[pos].probe_dist = 0;
	  break;
	}
      map->buckets[pos] = *nb;
      map->buckets[pos].probe_dist--;
      pos = next;
    }
  map->size--;
  return found;
}

/* t_revoke_map and j_logged reuse journal_block_map_t as a set and as a
   counter: the value slot holds a number instead of a buffer pointer.
   The number is never 0, so an empty slot still reads as NULL.  */
static inline journal_buffer_t *
journal_map_count_value (uintptr_t n)
{
  return (journal_buffer_t *) n;
}

static inline uintptr_t
journal_map_value_count (const journal_buffer_t *value)
{
  return (uintptr_t) value;
}

/**
 * Iterates over the block map.
 * Must be initialized to 0 before the first call.
 */
static journal_buffer_t *
journal_map_iterate (const journal_block_map_t *map, size_t *iter)
{
  if (!map->buckets)
    return NULL;

  while (*iter < map->capacity)
    {
      journal_bucket_t *bucket = &map->buckets[*iter];
      (*iter)++;

      if (bucket->jb != NULL)
	return bucket->jb;
    }

  return NULL;
}

/**
 * O(1) Slab Allocation.
 * MUST be called with JOURNAL_LOCK held.
 */
static inline journal_buffer_t *
journal_alloc_buffer (journal_t *journal)
{
  journal_buffer_t *jb;
  if (journal->j_free_buffers)
    {
      jb = journal->j_free_buffers;
      journal->j_free_buffers = jb->jb_next;

      /* Make sure we don't leak stale data from a previous transaction */
      jb->jb_next = NULL;
      jb->jb_is_written = 0;
      jb->needs_flush = 0;
      jb->jb_sweeping = 0;
      jb->jb_sweep_raced = 0;
      jb->jb_is_flushing = 0;
      jb->jb_escaped = 0;
      jb->jb_awaiting_data = 0;
      jb->jb_external = 0;
      jb->jb_editing = 0;
      return jb;
    }
  return calloc (1, sizeof (journal_buffer_t));
}

/**
 * O(1) Slab Deallocation.
 * MUST be called with JOURNAL_LOCK held.
 */
static inline void
journal_free_buffer (journal_t *journal, journal_buffer_t *jb)
{
  uintptr_t ptr = (uintptr_t) jb;
  uintptr_t start = (uintptr_t) journal->j_pool_memory;
  uintptr_t end = (uintptr_t) & journal->j_pool_memory[JRNL_MAX_FREE_BUFFERS];

  /* The flush after the commit, or a checkpoint, writes the shadow home
     first.  Only an aborted commit or a failed home write gets here with
     the flag set, and the block's latest contents are lost.  */
  if (jb->needs_flush)
    JRNL_LOG_WARN ("Dropping block %u before its deferred home write.",
		   jb->jb_blocknr);
  if (ptr >= start && ptr < end)
    {
      /* It belongs to the permanent pool. Link it back up! */
      jb->jb_next = journal->j_free_buffers;
      journal->j_free_buffers = jb;
    }
  else
    /* It falls outside the pool boundary. It was dynamically allocated. */
    free (jb);
}

static void
flush_to_disk (void)
{
  error_t err = store_sync (store);
  /* Warn on real I/O errors */
  if (err && err != EOPNOTSUPP && err != D_INVALID_OPERATION)
    ext2_warning ("device flush failed: %s", strerror (err));
}

static void
init_map (journal_t *journal, struct node *jnode)
{
  journal->map.total_blocks = jnode->allocsize / block_size;
  journal->map.phys_blocks =
    malloc (journal->map.total_blocks * sizeof (block_t));
  if (!journal->map.phys_blocks)
    ext2_panic ("No RAM for journal map");

  for (uint32_t i = 0; i < journal->map.total_blocks; i++)
    {
      block_t phys = 0;

      /* ext2_getblk handles the indirect blocks/fragmentation. */
      error_t err = ext2_getblk (jnode, i, 0, &phys);

      if (err || phys == 0)
	ext2_panic ("[JOURNAL] Gap in journal file at logical %u!", i);

      journal->map.phys_blocks[i] = phys;
    }

  journal->map.inode = jnode;
}

static block_t
get_journal_phys_block (journal_t *journal, uint32_t idx)
{
  assert_backtrace (idx < journal->map.total_blocks);
  return journal->map.phys_blocks[idx];
}

/* Centralized logic to map FS Block -> Store Offset */
static store_offset_t
journal_map_offset (journal_t *journal, uint32_t logical_idx)
{
  block_t phys_block = get_journal_phys_block (journal, logical_idx);
  /* Cast to 64-bit BEFORE shifting. to be safe on 32-bit architectures */
  return (store_offset_t) phys_block << (log2_block_size -
					 store->log2_block_size);
}

/**
 * Writes a full filesystem block (4096 bytes) to the journal.
 * Handles the Logical -> Physical -> Store Offset conversion.
 */
static error_t
journal_write_block (journal_t *journal, uint32_t logical_idx, void *data)
{
  store_offset_t offset;
  size_t written_amount = 0;
  error_t err;

  if (logical_idx >= journal->map.total_blocks)
    {
      JRNL_LOG_WARN ("Write out of bounds! Index: %u, Max: %u",
		     logical_idx, journal->map.total_blocks);
      return EINVAL;
    }

  offset = journal_map_offset (journal, logical_idx);
  err = store_write (store, offset, data, block_size, &written_amount);

  if (err)
    {
      JRNL_LOG_WARN ("Write failed at logical %u. Err: %s", logical_idx,
		     strerror (err));
      return err;
    }

  if (written_amount != block_size)
    {
      JRNL_LOG_WARN ("Short write! Wanted %u, wrote %zu", block_size,
		     written_amount);
      return EIO;
    }

  return 0;
}

/**
 * Reads a full filesystem block (4096 bytes) from the journal into 'out_buf'.
 * out_buf must be at least block_size bytes.
 */
static error_t
journal_read_block (journal_t *journal, uint32_t logical_idx, void *out_buf)
{
  store_offset_t offset;
  size_t read_amount = 0;
  error_t err;
  void *read_buf = out_buf;

  if (!out_buf)
    return EINVAL;

  if (logical_idx >= journal->map.total_blocks)
    {
      JRNL_LOG_WARN ("Read out of bounds! Index: %u, Max: %u", logical_idx,
		     journal->map.total_blocks);
      return EINVAL;
    }

  offset = journal_map_offset (journal, logical_idx);
  err = store_read (store, offset, block_size, &read_buf, &read_amount);

  if (err)
    return err;

  if (read_amount != block_size)
    {
      JRNL_LOG_WARN ("Short read! Wanted %u, got %zu", block_size,
		     read_amount);
      if (read_buf != out_buf)
	vm_deallocate (mach_task_self (), (vm_address_t) read_buf,
		       read_amount);
      return EIO;
    }

  if (read_buf != out_buf)
    {
      memcpy (out_buf, read_buf, block_size);
      vm_deallocate (mach_task_self (), (vm_address_t) read_buf, read_amount);
    }
  return 0;
}

/**
 * Reads the JBD2 superblock (Block 0 of the journal file)
 * and initializes the journal_t state.
 */
static error_t
journal_load_superblock (journal_t *journal)
{
  error_t err;
  journal_superblock_t *jsb;
  void *buf = malloc (block_size);
  if (!buf)
    return ENOMEM;

  /* journal_read_block handles all the store_read/vm_deallocate logic internally */
  err = journal_read_block (journal, 0, buf);

  if (err)
    {
      JRNL_LOG_WARN ("Failed to read SB. Err: %s", strerror (err));
      free (buf);
      return err;
    }

  /* Interpret as JBD2 Superblock and verify */
  jsb = (journal_superblock_t *) buf;
  uint32_t magic = be32toh (jsb->s_header[0]);
  uint32_t type = be32toh (jsb->s_header[1]);

  if (magic != JBD2_MAGIC_NUMBER)
    {
      JRNL_LOG_WARN ("Invalid Magic: %x (Expected %x)", magic,
		     JBD2_MAGIC_NUMBER);
      free (buf);
      return EINVAL;
    }

  /* Check versions */
  if (type == JBD2_SUPERBLOCK_V1)
    {
      JRNL_LOG_WARN ("Mounting V1 journal. 64-bit features disabled.");
      /* V1 ends at s_errno. Zero out all V2-specific fields. */
      jsb->s_feature_compat = 0;
      jsb->s_feature_incompat = 0;
      jsb->s_feature_ro_compat = 0;
      memset (jsb->s_uuid, 0, 16);
      jsb->s_nr_users = 0;
      jsb->s_dynsuper = 0;
      jsb->s_max_transaction = 0;
      jsb->s_max_trans_data = 0;
      jsb->s_checksum_type = 0;
      memset (jsb->s_padding2, 0, sizeof (jsb->s_padding2));
      jsb->s_checksum = 0;
      memset (jsb->s_users, 0, sizeof (jsb->s_users));
    }
  else if (type != JBD2_SUPERBLOCK_V2)
    {
      JRNL_LOG_WARN ("Invalid SB Type: %d", type);
      free (buf);
      return EINVAL;
    }

  /* Populate Journal Struct */
  journal->j_first = be32toh (jsb->s_first);
  journal->j_head = be32toh (jsb->s_start);
  journal->j_tail = journal->j_head;
  journal->j_disk_tail = journal->j_tail;
  journal->j_transaction_sequence = be32toh (jsb->s_sequence);

  /* Validate blocksize */
  uint32_t j_bsize = be32toh (jsb->s_blocksize);
  if (j_bsize != block_size)
    {
      JRNL_LOG_WARN ("Blocksize mismatch! Journal: %u, FS: %u", j_bsize,
		     block_size);
      free (buf);
      return EINVAL;
    }
  if (journal->j_first >= journal->map.total_blocks ||
      journal->j_head >= journal->map.total_blocks)
    {
      JRNL_LOG_WARN
	("Superblock bounds corrupted! First: %u, Head: %u, Max: %u",
	 journal->j_first, journal->j_head, journal->map.total_blocks);
      free (buf);
      return EINVAL;
    }

  jsb->s_maxlen = htobe32 (journal->map.total_blocks);

  journal->j_sb_buffer = buf;
  journal->j_last = journal->map.total_blocks - 1;
  journal->j_free = journal->j_last - journal->j_first;

  JRNL_LOG_DEBUG ("Loaded JBD2 Superblock:\n"
		  " - Sequence: %u\n"
		  " - Start (Head): %u\n"
		  " - First Data Block: %u\n"
		  " - Total Blocks: %u",
		  journal->j_transaction_sequence, journal->j_head,
		  journal->j_first, journal->j_last);
  return 0;
}

/**
 * Peeks at the next block in the ring buffer without allocating it.
 * Caller MUST hold JOURNAL_LOCK or guarantee exclusive access.
 */
static inline uint32_t
journal_next_block_would_be (const journal_t *journal)
{
  uint32_t next = journal->j_head + 1;
  if (next > journal->j_last)
    next = journal->j_first;
  return next;
}

/* Returns the free space in the log with the tail at TAIL: the blocks
   from the one after the head, the last written, up to the one before
   TAIL.  A TAIL of 0 means the log is empty, and so does a TAIL just after
   the head: a transaction placed there and not written yet, the oldest
   the log holds.  The log is never full, as
   journal_ensure_commit_space_locked keeps a margin.  */
static uint32_t
journal_free_for_tail (const journal_t *journal, uint32_t tail)
{
  uint32_t capacity = journal->j_last - journal->j_first + 1;
  uint32_t used;

  if (tail == 0 || tail == journal_next_block_would_be (journal))
    return capacity;
  if (journal->j_head >= tail)
    used = journal->j_head - tail + 1;
  else
    used = capacity - (tail - journal->j_head - 1);
  return capacity - used;
}

/* Count one more live transaction holding a copy of BLOCK.
   MUST be called with JOURNAL_LOCK held.  */
static void
journal_logged_ref_locked (journal_t *journal, block_t block)
{
  journal_bucket_t *bucket = journal_map_find (&journal->j_logged, block);

  if (bucket)
    {
      bucket->jb =
	journal_map_count_value (journal_map_value_count (bucket->jb) + 1);
      return;
    }

  if (!journal->j_logged.buckets
      || journal_map_insert (&journal->j_logged, block,
			     journal_map_count_value (1)))
    {
      if (!journal->j_logged_lost)
	JRNL_LOG_WARN ("ENOMEM tracking logged blocks; revoking every "
		       "freed block from now on.");
      journal->j_logged_lost = 1;
    }
}

/* A live transaction holding a copy of BLOCK goes away.
   MUST be called with JOURNAL_LOCK held.  */
static void
journal_logged_unref_locked (journal_t *journal, block_t block)
{
  journal_bucket_t *bucket = journal_map_find (&journal->j_logged, block);
  uintptr_t n;

  if (!bucket)
    {
      /* Only a reference whose insertion failed can be missing.  */
      assert_backtrace (journal->j_logged_lost);
      return;
    }

  n = journal_map_value_count (bucket->jb);
  if (n > 1)
    bucket->jb = journal_map_count_value (n - 1);
  else
    journal_map_remove (&journal->j_logged, block);
}

/* Returns whether BLOCK may have a copy in the log, so that freeing it
   needs a revoke record.  MUST be called with JOURNAL_LOCK held.  */
static int
journal_logged_locked (journal_t *journal, block_t block)
{
  return journal->j_logged_lost
    || journal_map_find (&journal->j_logged, block) != NULL;
}

/* Adds a revoke record for BLOCK to TXN.  MUST be called with
   JOURNAL_LOCK held.  */
static error_t
journal_revoke_locked (diskfs_transaction_t *txn, block_t block)
{
  journal_block_map_t *map = &txn->t_revoke_map;

  if (!map->buckets)
    {
      error_t err = journal_map_init (map, 0);
      if (err)
	return err;
    }
  if (journal_map_find (map, block))
    return 0;
  return journal_map_insert (map, block, journal_map_count_value (1));
}

/* Revoke records that fit in one revoke block.  */
static inline uint32_t
journal_revokes_per_block (void)
{
  return (block_size - sizeof (journal_revoke_header_t)) / sizeof (uint32_t);
}

/* Revoke blocks needed for TXN's revoke records.  */
static inline uint32_t
journal_revoke_blocks (const diskfs_transaction_t *txn)
{
  uint32_t per_block = journal_revokes_per_block ();

  return (txn->t_revoke_map.size + per_block - 1) / per_block;
}

/**
 * Fully destroys the transaction structure.
 * Called only when checkpointing is 100% complete.
 */
static void
journal_free_transaction (journal_t *journal, diskfs_transaction_t *txn)
{
  if (!txn)
    return;
  journal_freed_extent_t *ext = txn->t_freed_blocks;
  while (ext)
    {
      journal_freed_extent_t *next = ext->fe_next;
      free (ext);
      ext = next;
    }
  txn->t_freed_blocks = NULL;

  /* Free all journal buffers using the O(N) map iterator */
  size_t iter = 0;
  journal_buffer_t *jb;
  while ((jb = journal_map_iterate (&txn->t_buffer_map, &iter)) != NULL)
    {
      journal_logged_unref_locked (journal, jb->jb_blocknr);
      /* The slab allocator handles linking it back into the free pool */
      journal_free_buffer (journal, jb);
    }

  journal_map_destroy (&txn->t_buffer_map);
  journal_map_destroy (&txn->t_revoke_map);
  free (txn);
}

/**
 * Updates the JBD2 superblock with the given transaction sequence and
 * TAIL.  This issues a block write to the underlying store, but no
 * flush: what the superblock lets go must be on disk before it, and the
 * log space it frees reused only once it is (journal_write_tail_locked).
 *
 * MUST be called with JOURNAL_LOCK held.
 */
static error_t
journal_update_superblock (journal_t *journal, uint32_t sequence,
			   uint32_t tail)
{
  journal_superblock_t *jsb = (journal_superblock_t *) journal->j_sb_buffer;
  error_t err;

  jsb->s_sequence = htobe32 (sequence);
  jsb->s_start = htobe32 (tail);

  JRNL_LOG_DEBUG ("[SB] Updating: Seq %u, Head %u", sequence, tail);
  err = journal_write_block (journal, 0, jsb);
  if (!err)
    journal->j_disk_tail = tail;
  return err;
}

/* Whether tid T1 is later than T2, as the tids wrap.  */
static inline int
tid_gt (uint32_t t1, uint32_t t2)
{
  return (int32_t) (t1 - t2) > 0;
}

static diskfs_transaction_t *
journal_get_oldest_transaction_locked (journal_t *journal)
{
  if (journal->j_checkpoint_list)
    return journal->j_checkpoint_list;

  if (journal->j_committing_transaction)
    return journal->j_committing_transaction;

  if (journal->j_running_transaction)
    return journal->j_running_transaction;

  return NULL;
}

/* The journal, locked, or NULL if there is none or it stopped.  A call
   made on an open handle needs no j_must_exit check: the stop's final
   commit waits for the handle, and carries what it reserves.  The handle
   then keeps ext2_journal set too, unless the stop gave up waiting for it
   (journal_quiesce_checkpoints timed out): ext2_journal is cleared under
   the journal's lock with the handle still open, and the journal kept.
   So it is read once, and checked again under the lock, which it stays
   the same under.  */
static journal_t *
journal_lock_current (void)
{
  journal_t *journal = __atomic_load_n (&ext2_journal, __ATOMIC_ACQUIRE);

  if (!journal)
    return NULL;
  JOURNAL_LOCK (journal);
  if (ext2_journal == journal)
    return journal;
  JOURNAL_UNLOCK (journal);
  return NULL;
}

/**
 * Records a range of deleted blocks so they can be unpinned from older
 * checkpoint lists AFTER this transaction safely commits.  Returns 1 if
 * the range was recorded; journal_forget_freed_blocks then hands it back
 * to the allocator.  Returns 0 if it was not, and the caller must then not
 * let the blocks be reused.
 */
int
journal_record_freed_blocks (diskfs_transaction_t *txn, block_t start,
			     unsigned long count)
{
  journal_t *journal;

  if (!ext2_journal || !txn)
    return 0;

  journal_freed_extent_t *ext = malloc (sizeof (journal_freed_extent_t));
  if (!ext)
    {
      JRNL_LOG_WARN
	("ENOMEM tracking freed blocks %u[%lu].", start, count);
      return 0;
    }

  ext->fe_start = start;
  ext->fe_count = count;

  journal = journal_lock_current ();
  if (!journal)
    {
      free (ext);
      return 0;
    }
  if (txn->t_state != T_RUNNING && txn->t_state != T_LOCKED)
    {
      JRNL_LOG_DEBUG ("Cannot record freed blocks, no running transaction.");
      /* The transaction was committed by another thread before we locked!
         We just drop the recording, since the block is already forgotten. */
      JOURNAL_UNLOCK (journal);
      free (ext);
      return 0;
    }
  /* A freed block with a copy in the log gets a revoke record, so that
     recovery does not write that copy over the block's next owner.  Data
     blocks are never in the log and need none.  */
  for (unsigned long i = 0; i < count; i++)
    if (journal_logged_locked (journal, start + i)
	&& journal_revoke_locked (txn, start + i))
      {
	/* The caller keeps the range allocated.  The records added so far
	   only revoke blocks nothing points to any more.  */
	JRNL_LOG_WARN ("ENOMEM revoking freed blocks %u[%lu].", start, count);
	JOURNAL_UNLOCK (journal);
	free (ext);
	return 0;
      }

  ext->fe_next = txn->t_freed_blocks;
  txn->t_freed_blocks = ext;

  JOURNAL_UNLOCK (journal);
  return 1;
}

/**
 * Checks if the oldest transaction(s) are fully written.
 * If so, moves them to the retired list and advances the journal tail.
 * Their log space comes back once the caller has written the superblock
 * with the new tail (journal_release_retired_locked).
 */
static int
journal_try_advance_tail_locked (journal_t *journal)
{
  int advanced = 0;
  diskfs_transaction_t *txn = journal->j_checkpoint_list;

  JRNL_LOG_DEBUG ("Attempting to advance tail...");

  /* Loop until we hit a busy transaction or empty list */
  while (txn)
    {
      if (txn->t_outstanding_io > 0)
	break;

      JRNL_LOG_DEBUG ("[CHECKPOINT] Cascade Reclaim TID %u (0 pending).",
		      txn->t_tid);

      /* Unlink from list */
      journal->j_checkpoint_list = txn->t_checkpoint_next;
      if (journal->j_checkpoint_list == NULL)
	journal->j_checkpoint_last = NULL;

      txn->t_checkpoint_next = journal->j_retired_list;
      journal->j_retired_list = txn;

      advanced = 1;

      /* Reload the new head to check in the next iteration */
      txn = journal->j_checkpoint_list;
    }

  if (advanced)
    {
      diskfs_transaction_t *oldest =
	journal_get_oldest_transaction_locked (journal);
      journal->j_tail = oldest ? oldest->t_log_start : 0;
    }

  return advanced;
}


static void
journal_stop_transaction_locked (journal_t *journal,
				 diskfs_transaction_t *txn)
{
  if (txn->t_active_threads == 0)
    {
      /* This implies a double-stop or corruption */
      JRNL_LOG_WARN ("Logic Error: Transaction stopped too many times!");
      return;
    }
  txn->t_active_threads --;
  /* Continue until the map is proven clean while locked */
  while (txn->t_active_threads == 0)
    {
      size_t iter = 0;
      journal_buffer_t *jb_exp;
      journal_buffer_t *copy_list_head = NULL;
      journal_buffer_t *copy_list_tail = NULL;

      /* Traverse while map is locked */
      while ((jb_exp =
	      journal_map_iterate (&txn->t_buffer_map, &iter)) != NULL)
	{
	  if (jb_exp->needs_copy)
	    {
	      /* ALWAYS hydrate from the live VM cache.  A page the pager
	         wrote out in the meantime is read back from the shadow. */
	      /* Clear the flag here, under the lock, before the copy.  A
	         dirty that lands while we copy unlocked sets it again and
	         the next sweep recopies.  Clearing after the copy could
	         overwrite that re-arm and lose the recopy. */
	      assert_backtrace (!jb_exp->jb_external);
	      jb_exp->needs_copy = 0;
	      jb_exp->jb_sweeping = 1;
	      jb_exp->jb_sweep_raced = 0;
	      jb_exp->jb_next = NULL;
	      if (!copy_list_head)
		copy_list_head = jb_exp;
	      else
		copy_list_tail->jb_next = jb_exp;
	      copy_list_tail = jb_exp;
	    }
	}

      if (!copy_list_head)
	{
	  /* Map is perfectly clean. Wake the committer and break the loop. */
	  pthread_cond_broadcast (&journal->j_commit_wait);
	  break;
	}

      /* Now lockless hydration, we will update t_active_threads to > 0 so none can steal the txn. */
      txn->t_active_threads ++;
      JOURNAL_UNLOCK (journal);

      journal_buffer_t *curr = copy_list_head;
      while (curr)
	{
	  /* The copy runs unlocked, and the transaction is still
	     T_RUNNING: a handle can join meanwhile and edit the block
	     during the copy, which is then torn.  Such a copy never
	     commits.  The editor's journal_get_write_access and
	     journal_mark_dirty each set needs_copy again, and the last
	     handle to stop sweeps again with no editor left.  */
	  /* A reference, as the editor's is gone: the disk cache can have
	     let the block go since.  It does so only after the page is
	     evicted, and the pager write of an evicted page puts it in the
	     shadow, so then the shadow is current and there is nothing to
	     copy.  The block is not mapped again here: reclaiming a slot
	     waits for the pagers.  The superblock and the group
	     descriptors stay mapped, and cannot be referenced.  */
	  block_t sweep_block = curr->jb_blocknr;
	  void *cache_ref = NULL;
	  void *live_cache_ptr;
	  if (sweep_block >= group_desc_block_end)
	    live_cache_ptr = cache_ref
	      = disk_cache_block_ref_mapped (sweep_block);
	  else
	    live_cache_ptr = bptr (sweep_block);
	  /**
          * We execute exactly ONE memory copy per block,
          * capturing the fully settled, tear-free state of the RAM.
          * We do this even if jb_is_written == 1, because if the pager
          * rushed the block, we MUST capture this settled state into the
          * WAL so it can overwrite the pager's rushed data during recovery!
          */
	  pthread_mutex_lock (&journal_sweep_scratch_lock);
	  if (live_cache_ptr)
	    memcpy (journal_sweep_scratch, live_cache_ptr, block_size);
	  if (cache_ref)
	    disk_cache_block_deref (cache_ref);
	  /* needs_copy was cleared under the lock when this block was
	   * listed.  If the block is modified again, journal_get_write_access
	   * sets it back to 1 and a later sweep recopies. */

	  /* The copy reaches the shadow under the lock.  A pager write
	     that put the page in the shadow while we copied holds the
	     newer bytes: an edit made after our read of them was evicted
	     with the page, and copying would bring the old ones back.  */
	  JOURNAL_LOCK (journal);
	  if (live_cache_ptr && !curr->jb_sweep_raced)
	    memcpy (curr->jb_shadow_data, journal_sweep_scratch, block_size);
	  curr->jb_sweeping = 0;
	  curr->jb_sweep_raced = 0;
	  journal_buffer_t *next = curr->jb_next;
	  curr->jb_next = NULL;
	  JOURNAL_UNLOCK (journal);
	  pthread_mutex_unlock (&journal_sweep_scratch_lock);
	  curr = next;
	}

      JOURNAL_LOCK (journal);
      /* We need to decrement what we incremented. */
      txn->t_active_threads--;

      /* If t_active_threads is still 0, the loop repeats to verify no blocks were
         added. If t_active_threads > 0, another thread joined while we were unlocked
         and they will handle the final sweep. We safely fall out. */
    }
}

/**
 * Checks a range of written blocks against a single transaction's map.
 * Marks any matching buffers as written and decrements the outstanding I/O
 * counter. MUST be called with JOURNAL_LOCK held.
 * Returns 1 if the transaction's counter reached zero, 0 otherwise.
 */
static int
journal_notify_txn_locked (journal_t *journal, diskfs_transaction_t *txn,
			   block_t start_block, size_t n_blocks)
{
  /* Process all blocks to ensure jb_is_flushing is safely cleared everywhere */
  for (size_t i = 0; i < n_blocks; i++)
    {
      block_t b = start_block + i;
      journal_buffer_t *jb = journal_map_lookup (&txn->t_buffer_map, b);

      if (jb)
	{
	  /* What went home is the page, at least as new as the shadow: a
	     home write still due after a failed one is not any more.  */
	  jb->needs_flush = 0;
	  if (!jb->jb_is_written)
	    {
	      jb->jb_is_written = 1;
	      if (txn->t_outstanding_io > 0)
		txn->t_outstanding_io--;

	      JRNL_LOG_DEBUG
		("[NOTIFY] Block %u written for TID %u (outstanding: %d)", b,
		 txn->t_tid, txn->t_outstanding_io);
	    }

	  /* Pager finished the I/O. Unblock Checkpoint thread! */
	  if (jb->jb_is_flushing)
	    {
	      jb->jb_is_flushing = 0;
	      pthread_cond_broadcast (&journal->j_flush_wait);
	    }
	}
    }
  return txn->t_outstanding_io == 0;
}

/* Write the tail to the journal superblock, as jbd2 does, after
   journal_try_advance_tail_locked moved it.  The home writes that let the
   retired transactions go are flushed first: a superblock past them on
   disk without them loses what they hold.  Their log space comes back
   only once the superblock is flushed too: until then the old one can
   be what the disk has, and the log must still hold what it points to.
   Transactions retired while the lock is dropped stay in the log, since
   the first flush may have missed their home writes.  Returns 0 or the
   error of the superblock write, after which the retired transactions
   are kept for the next call.  MUST be called with JOURNAL_LOCK held;
   drops it.  */
static error_t
journal_write_tail_locked (journal_t *journal)
{
  diskfs_transaction_t *retired, *oldest, *txn;
  error_t err;

  while (journal->j_tail_writing)
    JOURNAL_WAIT (&journal->j_tail_written, journal);
  retired = journal->j_retired_list;
  if (!retired)
    return 0;
  journal->j_retired_list = NULL;
  journal->j_tail_writing = 1;

  JOURNAL_UNLOCK (journal);
  flush_to_disk ();
  JOURNAL_LOCK (journal);

  oldest = journal_get_oldest_transaction_locked (journal);
  for (txn = journal->j_retired_list; txn; txn = txn->t_checkpoint_next)
    if (!oldest || tid_gt (oldest->t_tid, txn->t_tid))
      oldest = txn;
  uint32_t tail = oldest ? oldest->t_log_start : 0;
  err = journal_update_superblock (journal, oldest ? oldest->t_tid
				   : journal->j_transaction_sequence, tail);
  if (!err)
    {
      JOURNAL_UNLOCK (journal);
      flush_to_disk ();
      JOURNAL_LOCK (journal);
      while (retired)
	{
	  txn = retired->t_checkpoint_next;
	  journal_free_transaction (journal, retired);
	  retired = txn;
	}
      journal->j_free = journal_free_for_tail (journal, tail);
      JRNL_LOG_DEBUG ("[CHECKPOINT] Tail on disk. New Free: %u",
		      journal->j_free);
    }
  else
    {
      JRNL_LOG_WARN ("Failed to update superblock. %s", strerror (err));
      for (txn = retired; txn->t_checkpoint_next; txn = txn->t_checkpoint_next)
	;
      txn->t_checkpoint_next = journal->j_retired_list;
      journal->j_retired_list = retired;
    }
  journal->j_tail_writing = 0;
  pthread_cond_broadcast (&journal->j_tail_written);
  return err;
}

/* journal_try_advance_tail_locked moved the tail: have kjournald write
   it.  MUST be called with JOURNAL_LOCK held.  */
static void
journal_tail_moved_locked (journal_t *journal)
{
  pthread_cond_signal (&journal->j_flusher_wakeup);
}

/**
 * Called just after blocks have been written to the main disk.  A tail
 * that moves is written by kjournald, after a flush.
 */
static void
journal_notify_blocks_written_locked (journal_t *journal, block_t start_block, size_t n_blocks)
{
  int sb_changed = 0;
  if (n_blocks == 0)
    return;

  /* On shutdown journal_quiesce_checkpoints owns the checkpoint list and
     drops the lock for I/O while it walks it, so nothing here may unlink or
     free a transaction.  The blocks are still marked written and their
     jb_is_flushing claims released: quiesce waits on j_flush_wait for any
     block a pager write has claimed.  */
  int exiting = journal->j_must_exit;

  JRNL_LOG_DEBUG ("Got notification for %zu blocks starting at %u",
		  n_blocks, start_block);

  /* Do NOT notify the running transaction here.
   *
   * The running transaction has not yet crossed the WAL barrier — its shadow
   * buffers may still be updated by journal_stop_transaction_locked as VFS
   * threads continue to modify blocks.  If we mark a block jb_is_written=1 in
   * the running transaction, a later checkpoint will skip writing the (correct)
   * shadow data to the main disk, permanently leaving stale metadata behind.
   *
   * This can happen due to a race: the pager checks hazards (block not in any
   * transaction), unlocks for physical I/O, then a VFS thread adds the block to
   * the running transaction.  When the pager re-locks and calls us, the block
   * IS in the running transaction, but the data we just wrote to disk may be
   * stale relative to the final shadow copy.
   *
   * Blocks in the running transaction that legitimately need to be marked as
   * written are handled by journal_flush_intercepted_payloads(), which runs AFTER
   * the WAL barrier is crossed (T_COMMITTED) and does its own marking.
   *
   * The same holds for the committing transaction until it is T_COMMITTED:
   * while it drains (T_LOCKED) it still takes reservations from the handles
   * open on it, and the same race applies.  Before T_COMMITTED every block
   * in its map is intercepted, so a home write that finds one there wrote
   * data from before the reservation.  From T_COMMITTED on its shadows are
   * frozen and on the log, as are the checkpoint list's, and they are safe
   * to notify. */

  /* Check Committing Transaction */
  diskfs_transaction_t *commit = journal->j_committing_transaction;
  if (commit && commit->t_state == T_COMMITTED)
    journal_notify_txn_locked (journal, commit, start_block, n_blocks);

  /* Iterate over checkpoint list */
  diskfs_transaction_t *txn = journal->j_checkpoint_list;
  while (txn)
    {
      if (exiting)
	{
	  journal_notify_txn_locked (journal, txn, start_block, n_blocks);
	  txn = txn->t_checkpoint_next;
	  continue;
	}

      /* A head with nothing left to write goes, and those after it with
	 nothing left either.  One already at 0 here (a commit made it the
	 head with all its blocks home, and has yet to advance the tail
	 itself) needs no notice, and has no claim to clear.  */
      if (journal_notify_txn_locked (journal, txn, start_block, n_blocks)
	  && txn == journal->j_checkpoint_list)
	{
	  if (journal_try_advance_tail_locked (journal))
	    sb_changed = 1;
	  txn = journal->j_checkpoint_list;
	  continue;
	}

      txn = txn->t_checkpoint_next;
    }
  if (sb_changed)
    journal_tail_moved_locked (journal);
}

/**
 * Consumes the freed blocks list of transaction FREED_TID once it has
 * committed.  The copies of those blocks in FREED_TID and in older
 * checkpoint transactions hold the old contents, so they need no home
 * write anymore and are marked written.  The allocator keeps the blocks
 * busy until here, so no copy can belong to a new owner yet.  The blocks
 * then go back to the allocator.
 */
static void
journal_forget_freed_blocks (journal_t *journal, uint32_t freed_tid,
			     journal_freed_extent_t *freed)
{
  journal_freed_extent_t *ext;

  JOURNAL_LOCK (journal);
  for (ext = freed; ext; ext = ext->fe_next)
    {
    restart:
      for (diskfs_transaction_t *txn = journal->j_checkpoint_list;
	   txn && !tid_gt (txn->t_tid, freed_tid);
	   txn = txn->t_checkpoint_next)
	for (unsigned long i = 0; i < ext->fe_count; i++)
	  {
	    block_t b = ext->fe_start + i;
	    journal_buffer_t *jb =
	      journal_map_lookup (&txn->t_buffer_map, b);
	    if (!jb)
	      continue;
	    if (jb->jb_is_flushing)
	      {
		/* A write of the old contents is in flight.  It must land
		   before the block has a new owner.  The wait drops the
		   lock, and the checkpoint list can change meanwhile.  */
		JOURNAL_WAIT (&journal->j_flush_wait, journal);
		goto restart;
	      }
	    journal_notify_txn_locked (journal, txn, b, 1);
	  }
    }

  /* On shutdown journal_quiesce_checkpoints owns the checkpoint list.  */
  if (!journal->j_must_exit && journal_try_advance_tail_locked (journal))
    journal_tail_moved_locked (journal);
  JOURNAL_UNLOCK (journal);

  /* ext2_new_block takes the journal lock inside global_lock, so the
     blocks go back to the allocator with the journal lock dropped.  */
  while (freed)
    {
      ext = freed->fe_next;
      ext2_release_busy_blocks (freed->fe_start, freed->fe_count);
      free (freed);
      freed = ext;
    }

  /* Releases go in transaction order, so that j_released_tid means that
     the frees of every transaction up to it are back.  The next commit
     can finish, and its release too, while this one waits for a pager
     write or for global_lock above; setting j_released_tid back to
     FREED_TID then would leave a thread waiting for that commit's frees
     (journal_wait_freed_blocks) waiting for good, maybe with a node
     lock its own transaction's commit waits for.  */
  JOURNAL_LOCK (journal);
  while (tid_gt (freed_tid, journal->j_released_tid + 1))
    JOURNAL_WAIT (&journal->j_commit_done, journal);
  /* An aborted commit after this one has set it already.  */
  if (tid_gt (freed_tid, journal->j_released_tid))
    journal->j_released_tid = freed_tid;
  pthread_cond_broadcast (&journal->j_commit_done);
  JOURNAL_UNLOCK (journal);
}

/* Install a running transaction with t_active_threads == 0.
   The journal lock is held on entry and on return, including failure.
   j_running_transaction must be NULL.  This does not drop the lock, so
   commit can publish the successor with no gap for start to observe. */
static diskfs_transaction_t *
journal_create_running_transaction_locked (journal_t *journal)
{
  diskfs_transaction_t *txn;

  assert_backtrace (!journal->j_running_transaction);

  txn = calloc (1, sizeof (diskfs_transaction_t));
  if (!txn)
    return NULL;

  if (journal_map_init (&txn->t_buffer_map, 0) != 0)
    {
      free (txn);
      return NULL;
    }

  txn->t_tid = journal->j_transaction_sequence++;
  txn->t_state = T_RUNNING;
  txn->t_active_threads = 0;

  journal->j_running_transaction = txn;
  JRNL_LOG_DEBUG ("[TRX] Created NEW TID %u", txn->t_tid);
  return txn;
}

/**
 * Forcefully clears the checkpoint list.
 * SAFE ONLY after a full filesystem sync.
 */
static error_t
journal_clear_checkpoint_list_locked (journal_t *journal)
{
  diskfs_transaction_t *txn = journal->j_checkpoint_list;
  error_t err = 0;
  /* Destroy all checkpoint transactions */
  while (txn)
    {
      diskfs_transaction_t *next = txn->t_checkpoint_next;
      journal_free_transaction (journal, txn);
      txn = next;
    }
  journal->j_checkpoint_list = NULL;
  journal->j_checkpoint_last = NULL;

  diskfs_transaction_t *oldest =
    journal_get_oldest_transaction_locked (journal);

  journal->j_tail = oldest ? oldest->t_log_start : 0;
  journal->j_free = journal_free_for_tail (journal, journal->j_tail);

  err = journal_update_superblock (journal, journal->j_transaction_sequence,
				   journal->j_tail);
  if (err)
    JRNL_LOG_WARN ("Failed to update superblock. %s", strerror (err));
  return err;
}

/**
 * Checks if a block exists in any transaction newer than 'txn'.
 * This prevents older checkpoints from overwriting fresh data on the disk.
 */
static int
journal_is_block_in_newer_transaction_locked (journal_t *journal,
					      diskfs_transaction_t *txn,
					      block_t b)
{
  /* Check all transactions in the checkpoint list newer than 'txn'.
     These are fully committed to the WAL, so relying on them is safe. */
  diskfs_transaction_t *t = txn->t_checkpoint_next;
  while (t)
    {
      if (journal_map_lookup (&t->t_buffer_map, b))
	return 1;
      t = t->t_checkpoint_next;
    }

  /* Check the committing transaction ONLY if it has safely crossed the WAL
     barrier. If it is still T_FLUSHING or T_LOCKED, a crash would lose it,
     so we cannot rely on it to skip physical I/O! */
  t = journal->j_committing_transaction;
  if (t && t->t_state == T_COMMITTED
      && journal_map_lookup (&t->t_buffer_map, b))
    return 1;

  /* NEVER check the running transaction. It is not on disk yet.
     Relying on it would permanently delete the older safely committed WAL backup
     before the new one is written, causing unrecoverable data loss on crash! */

  return 0;
}

/* Whether a checkpoint is writing home an older copy of block B: a
   transaction in the checkpoint list has it claimed.  MUST be called with
   JOURNAL_LOCK held.  */
static int
journal_older_copy_flushing_locked (journal_t *journal, block_t b)
{
  for (diskfs_transaction_t *t = journal->j_checkpoint_list; t;
       t = t->t_checkpoint_next)
    {
      journal_buffer_t *jb = journal_map_lookup (&t->t_buffer_map, b);
      if (jb && jb->jb_is_flushing)
	return 1;
    }
  return 0;
}

/* Write home the blocks of TXN whose pager writes were taken into their
   shadows (needs_flush).  Executes outside the journal lock, which it
   takes per block.  Called right after TXN is committed to the log.  */
static void
journal_flush_intercepted_payloads (journal_t *journal,
				    diskfs_transaction_t *txn)
{
  size_t iter = 0;
  journal_buffer_t *jb;

  while ((jb = journal_map_iterate (&txn->t_buffer_map, &iter)) != NULL)
    {
      int flush = 0;

      JOURNAL_LOCK (journal);
      /* A checkpoint can be writing an older copy of the block: it
	 checked for a newer one while TXN was not committed yet, and must
	 land first, or it would land over this one, and both count as
	 written.  The pager waits for such a claim too.  */
      while (jb->needs_flush
	     && journal_older_copy_flushing_locked (journal, jb->jb_blocknr))
	JOURNAL_WAIT (&journal->j_flush_wait, journal);
      if (jb->needs_flush)
	{
	  flush = 1;
	  jb->jb_is_flushing = 1;
	}
      JOURNAL_UNLOCK (journal);

      if (flush)
	{
	  store_offset_t dev_block =
	    (store_offset_t) jb->jb_blocknr << log2_dev_blocks_per_fs_block;
	  size_t amount;
	  error_t err;

	  /* Shadow data is fully settled and committed. */
	  err =
	    store_write (store, dev_block, jb->jb_shadow_data, block_size, &amount);

	  JOURNAL_LOCK (journal);
	  if (err)
	    JRNL_LOG_WARN ("Intercept flush failed for block %u: %s",
			   jb->jb_blocknr, strerror (err));

	  jb->jb_is_flushing = 0;
	  pthread_cond_broadcast (&journal->j_flush_wait);

	  /* The shadow is home now, and reads go to the disk again.  The
	     flag is cleared only after the write has returned, so that a
	     read that finds it clear finds the new contents at home.  On
	     an error it stays set: reads keep using the shadow, and the
	     checkpoint writes it home.  */
	  if (!err)
	    {
	      jb->needs_flush = 0;
	      journal_notify_blocks_written_locked (journal, jb->jb_blocknr, 1);
	    }

	  JOURNAL_UNLOCK (journal);
	}
    }
}

/* Whether a background flush should give way: the journal is shutting
   down (quiesce owns the list), a stop asked for a commit, or a commit is
   in flight and may be waiting for this flush.  */
static int
journal_checkpoint_should_yield_locked (journal_t *journal)
{
  return journal->j_must_exit || journal->j_commit_requested
    || journal->j_committing_transaction;
}

/**
 * Internal helper to flush checkpoint transactions to the main filesystem.
 * If target_free is UINT32_MAX, it flushes ALL transactions in the list.
 * Otherwise, it flushes until j_free >= target_free.
 * A BACKGROUND flush also stops between transactions when
 * journal_checkpoint_should_yield_locked says so.
 * Only one flusher runs at a time; a second one waits for the first, then
 * flushes only what is still needed.
 * Returns 1 if a hardware I/O error occurred, 0 on success.
 * MUST be called with JOURNAL_LOCK held.
 */
static int
journal_flush_checkpoints_locked (journal_t *journal, uint32_t target_free,
				  int background)
{
  int global_io_error = 0;

  while (journal->j_checkpointing)
    JOURNAL_WAIT (&journal->j_checkpoint_done, journal);
  journal->j_checkpointing = 1;

  /* j_free grows only once the superblock with the new tail is written,
     after the loop, so measure progress by the tail.  */
  while (journal_free_for_tail (journal, journal->j_tail) < target_free
	 && journal->j_checkpoint_list
	 && !(background && journal_checkpoint_should_yield_locked (journal)))
    {
      diskfs_transaction_t *txn = journal->j_checkpoint_list;
      size_t iter = 0;
      journal_buffer_t *jb;
      int io_error = 0;

      /* Pin TXN.  The lock is dropped for I/O and while waiting for a
	 pager claim; a pager write that completes the last outstanding
	 blocks would otherwise let journal_notify_blocks_written_locked
	 advance the tail and free TXN under us.  */
      txn->t_outstanding_io++;

      /* Stream the fully committed WAL shadow blocks directly to the metal */
      while ((jb = journal_map_iterate (&txn->t_buffer_map, &iter)) != NULL)
	{
	retry_block:
	  if (!jb->jb_is_written)
	    {
	      if (jb->jb_is_flushing)
		{
		  JOURNAL_WAIT (&journal->j_flush_wait, journal);
		  goto retry_block;
		}

	      jb->jb_is_flushing = 1;
	      block_t b = jb->jb_blocknr;
	      char *data = jb->jb_shadow_data;
	      size_t amount = 0;
	      error_t err = 0;

	      if (journal_is_block_in_newer_transaction_locked
		  (journal, txn, b))
		{
		  /* A newer transaction already captured this block.
		     Skip physical I/O to protect the fresh data on disk. */
		  amount = block_size;
		}
	      else
		{
		  /* Unlock to perform physical I/O without stalling the journal */
		  JOURNAL_UNLOCK (journal);
		  store_offset_t dev_block =
		    (store_offset_t) b << log2_dev_blocks_per_fs_block;
		  err =
		    store_write (store, dev_block, data, block_size, &amount);
		  JOURNAL_LOCK (journal);
		}

	      /* ONLY mark as written if the hardware actually accepted the full block! */
	      if (!err && amount == block_size)
		{
		  /* The shadow is home: a failed flush after the commit
		     is made good.  */
		  jb->needs_flush = 0;
		  if (!jb->jb_is_written)
		    {
		      jb->jb_is_written = 1;
		      if (txn->t_outstanding_io > 0)
			txn->t_outstanding_io--;
		    }
		}
	      else
		{
		  JRNL_LOG_WARN
		    ("Checkpoint I/O failed for block %u! err=%d, wrote=%zu",
		     b, err, amount);
		  io_error = 1;
		}

	      jb->jb_is_flushing = 0;
	      pthread_cond_broadcast (&journal->j_flush_wait);

	      if (io_error)
		break;
	    }
	}

      txn->t_outstanding_io--;

      /* All blocks are physically on disk. Reclaim the space!  TXN is
	 the head, and pinned until just above, so nothing else retired
	 it, and every block of it is written.  */
      if (io_error)
	{
	  global_io_error = 1;
	  break;		/* Stop checkpointing on device failure */
	}
      if (!journal_try_advance_tail_locked (journal))
	assert_backtrace (!"a checkpointed transaction could not retire");
    }

  /* Also write it when an earlier superblock write failed and left
     transactions retired, or one moved by a home write waits, so that
     their log space comes back.  */
  journal_write_tail_locked (journal);

  journal->j_checkpointing = 0;
  pthread_cond_broadcast (&journal->j_checkpoint_done);

  return global_io_error;
}

/**
 * Actively flushes the oldest checkpointed transactions to the main filesystem.
 * This guarantees space is freed instantly without relying on the lazy Mach pager,
 * and executes entirely without acquiring VFS node locks.
 */
static void
journal_force_checkpoint_locked (journal_t *journal)
{
  JRNL_LOG_DEBUG
    ("[CHECKPOINT] Journal Full (Free: %u). Actively checkpointing...",
     journal->j_free);

  /* Add a 1/8th safety runway to prevent thrashing */
  uint32_t runway = (journal->j_last - journal->j_first) / 8;
  uint32_t target_free = journal->j_min_free + runway;

  journal_flush_checkpoints_locked (journal, target_free, 0);

  JRNL_LOG_DEBUG ("[CHECKPOINT] Done checkpointing (Free: %u).",
		  journal->j_free);
}

/* Whether kjournald should checkpoint in the background: less than
   1/JRNL_BG_START_RATIO of the journal is free and there is something to
   flush.  */
static int
journal_bg_checkpoint_needed_locked (journal_t *journal)
{
  uint32_t capacity = journal->j_last - journal->j_first;

  return journal->j_checkpoint_list && !journal->j_must_exit
    && !diskfs_readonly
    && journal->j_free < capacity / JRNL_BG_START_RATIO;
}

/* Checkpoint the oldest transactions until 1 - 1/JRNL_BG_STOP_RATIO of the
   journal is free, so that commit rarely has to checkpoint itself.  Gives
   way to commits and to shutdown between transactions.  Called by
   kjournald.  */
static void
journal_bg_checkpoint_locked (journal_t *journal)
{
  uint32_t capacity = journal->j_last - journal->j_first;

  if (!journal_bg_checkpoint_needed_locked (journal))
    return;

  JRNL_LOG_DEBUG ("[CHECKPOINT] Background checkpoint (Free: %u).",
		  journal->j_free);
  journal_flush_checkpoints_locked (journal,
				    capacity - capacity / JRNL_BG_STOP_RATIO,
				    1);
  JRNL_LOG_DEBUG ("[CHECKPOINT] Background checkpoint done (Free: %u).",
		  journal->j_free);
}

static inline uint32_t
journal_next_log_block_safe (journal_t *journal)
{
  uint32_t block;
  JOURNAL_LOCK (journal);

  journal->j_head = journal_next_block_would_be (journal);
  journal->j_free--;
  block = journal->j_head;

  JOURNAL_UNLOCK (journal);
  return block;
}

static inline void
journal_wait_on_tid_locked (journal_t *journal, uint32_t target_tid)
{
  while (tid_gt (target_tid, journal->j_last_committed_tid))
    /* Sleep until a commit finishes */
    JOURNAL_WAIT (&journal->j_commit_done, journal);
}

/* Wait until the frees of the committing transaction, or of one that
   has committed but not given them back yet, are back with the
   allocator.  A pager thread never waits.  Neither does a thread whose
   handle is on the committing transaction, since that commit waits for
   the handle to drain.  The running transaction's frees wait for its
   commit, which the caller's handle holds back; kjournald is woken to
   start that commit as soon as the handle drains.  */
int
journal_wait_freed_blocks (diskfs_transaction_t *txn)
{
  diskfs_transaction_t *commit;
  journal_t *journal;
  uint32_t tid = 0;
  int wait = 0;

  if (journal_thread_is_pager)
    return 0;
  /* Called with no handle too (diskfs_journal_should_retry_alloc).  */
  journal = journal_lock_current ();
  if (!journal)
    return 0;

  pthread_cond_signal (&journal->j_flusher_wakeup);
  commit = journal->j_committing_transaction;
  if (commit && commit != txn)
    {
      tid = commit->t_tid;
      wait = 1;
    }
  else if (tid_gt (journal->j_last_committed_tid, journal->j_released_tid))
    {
      /* Committed: its frees come back once journal_forget_freed_blocks
	 is done, which needs nothing the caller can hold.  */
      tid = journal->j_last_committed_tid;
      wait = 1;
    }
  if (wait)
    while (tid_gt (tid, journal->j_released_tid))
      JOURNAL_WAIT (&journal->j_commit_done, journal);
  JOURNAL_UNLOCK (journal);
  return wait;
}

/* Whether an operation that got ENOSPC should try again, as
   ext4_should_retry_alloc.  Blocks freed by a transaction that has not
   committed, or not given them back yet, are busy, and the allocator
   cannot wait for them under the caller's handle: an operation whose own
   transaction freed them gets ENOSPC although they come back at the next
   commit.  The caller has let go of its handle and node locks, so commit
   the running transaction here and wait for the freed blocks, at most
   three times an operation (*RETRIES).  Only no free block at all, busy
   ones included, means that the space is gone: the commit can have
   given the freed blocks back since the allocation failed, and then
   nothing is busy any more, though they are what the caller needs.  */
int
diskfs_journal_should_retry_alloc (int *retries)
{
  diskfs_transaction_t *txn;

  if (!ext2_journal || journal_thread_is_pager || journal_thread_depth > 0)
    return 0;
  if (*retries >= 3 || ext2_free_blocks_now () == 0)
    return 0;
  (*retries)++;

  /* None while the journal stops: its final commit is the caller's
     last chance, which it waits for elsewhere.  */
  txn = diskfs_journal_start_transaction ();
  if (!txn || diskfs_journal_commit_transaction (txn))
    return 0;
  journal_wait_freed_blocks (NULL);
  return 1;
}

void
journal_mark_dirty (diskfs_transaction_t *txn, block_t fs_blocknr)
{
  journal_t *journal;

  if (!txn || !(journal = journal_lock_current ()))
    return;

  journal_buffer_t *jb = journal_map_lookup (&txn->t_buffer_map, fs_blocknr);
  if (!jb)
    {
      /* STRICT JBD2 BEHAVIOR:
         If we hit this, the filesystem modified a block without calling
         journal_get_write_access() first! */
      JRNL_LOG_WARN ("mark_dirty called on unreserved block %u! Missing get_write_access?",
                     fs_blocknr);
      goto out;
    }

  /* The sweep copies the new edit.  needs_flush stays as it is: until the
     flush after the commit, the home copy is stale either way, and a page
     written out since is read back from the shadow.  A directory block's
     edits come through journal_mark_dirty_external instead: the sweep
     would copy the disk cache's stale bytes.  */
  assert_backtrace (!jb->jb_external);
  if (jb->jb_external)
    JRNL_LOG_WARN ("mark_dirty called on directory block %u.", fs_blocknr);
  else
    jb->needs_copy = 1;

  /* Only committed transactions are notified of home writes, and a
     transaction with handles open has not committed.  */
  assert_backtrace (!jb->jb_is_written);

out:
  JOURNAL_UNLOCK (journal);
}

/**
 * Adds a modified filesystem block to the SPECIFIC transaction handle.
 * With HYDRATE set, defers the actual memory copy until the transaction
 * stops.  With HYDRATE clear, the block's live bytes are outside the disk
 * cache, and journal_mark_dirty_external supplies the copy.
 */
static error_t
journal_get_write_access_locked (journal_t *journal, diskfs_transaction_t *txn,
				 block_t fs_blocknr, int hydrate)
{
  journal_buffer_t *jb;
  journal_buffer_t *new_jb;
  error_t err = 0;

  assert_backtrace (txn->t_state == T_RUNNING || txn->t_state == T_LOCKED);

  /* A revoke record in TXN would make recovery skip the copy TXN is about
     to take as well.  Freed blocks stay busy until their free commits, so
     this should not happen.  */
  if (txn->t_revoke_map.size
      && journal_map_remove (&txn->t_revoke_map, fs_blocknr))
    JRNL_LOG_WARN ("Block %u freed and journaled again in transaction %u; "
		   "dropping its revoke record.", fs_blocknr, txn->t_tid);

  jb = journal_map_lookup (&txn->t_buffer_map, fs_blocknr);

  if (jb)
    {
      /* Even if it is already contained in the map we have still been
       * notified about the new change and therefore we must copy it
       * over once more when the time comes. */
      /* A directory block is never reserved again through the disk
	 cache, whose copy of it is stale.  */
      assert_backtrace (!(hydrate && jb->jb_external));
      if (!hydrate)
	{
	  jb->jb_external = 1;
	  jb->jb_editing = 1;
	}
      if (hydrate)
	jb->jb_awaiting_data = 0;
      else if (jb->needs_copy)
	/* The shadow is due to be copied from the disk cache, which does
	 * not hold the caller's edit. */
	jb->jb_awaiting_data = 1;
      jb->needs_copy = hydrate && !jb->jb_external;
      /* As in journal_mark_dirty.  */
      assert_backtrace (!jb->jb_is_written);
      goto out;
    }

  new_jb = journal_alloc_buffer (journal);
  if (!new_jb)
    {
      err = ENOMEM;
      goto out;
    }

  new_jb->jb_blocknr = fs_blocknr;
  err = journal_map_insert (&txn->t_buffer_map, fs_blocknr, new_jb);
  if (err)
    {
      journal_free_buffer (journal, new_jb);
      goto out;
    }

  /* Brand new block for us? We will definitively want to copy it over. */
  new_jb->needs_copy = hydrate;
  new_jb->jb_awaiting_data = !hydrate;
  new_jb->jb_external = !hydrate;
  new_jb->jb_editing = !hydrate;
  txn->t_outstanding_io++;
  journal_logged_ref_locked (journal, fs_blocknr);
out:
  return err;
}

/* Join the running transaction.  Only the outermost start on a thread
   comes here; nested starts are served from journal_thread_txn.

   While commit drains a transaction (T_LOCKED), j_running_transaction is
   NULL and the successor does not exist yet.  An RPC thread waits here
   until the successor is published.  The wait is safe because an RPC
   takes its outermost start before any node lock, so a waiting starter
   holds nothing a draining participant needs.  A pager thread joins the
   draining transaction instead: it must never wait on the journal, and
   its work is one bounded operation that only extends the drain.

   Nothing else is ever observed here: commit publishes the successor
   before it drops the lock, so a NULL j_running_transaction with no
   commit in flight means the successor could not be allocated.  Shutdown
   is the exception: j_must_exit means commit did not install a
   successor, and the caller must tolerate NULL. */
static diskfs_transaction_t *
journal_join_transaction_locked (journal_t *journal)
{
  diskfs_transaction_t *txn;

  while (!(txn = journal->j_running_transaction))
    {
      diskfs_transaction_t *commit = journal->j_committing_transaction;

      if (journal->j_must_exit)
	return NULL;

      assert_backtrace (commit);

      if (journal_thread_is_pager && commit->t_state == T_LOCKED)
	{
	  txn = commit;
	  break;
	}

      JOURNAL_WAIT (&journal->j_running_wait, journal);
    }

  if (journal->j_must_exit)
    return NULL;

  assert_backtrace (txn->t_state == T_RUNNING
		    || (journal_thread_is_pager && txn->t_state == T_LOCKED));
  txn->t_active_threads ++;
  return txn;
}

/* Called by the ext2fs pager callbacks on entry (ON = 1) and exit
   (ON = 0).  See journal_thread_is_pager. */
void
journal_thread_set_pager (int on)
{
  journal_thread_is_pager = on;
}

/* Open a handle on the running transaction for this thread.  The outermost
   start of an RPC may wait while a commit drains the previous transaction;
   it must therefore be taken before any node lock.  A pager thread never
   waits here.  Nested starts return the thread's transaction at once. */
diskfs_transaction_t *
diskfs_journal_start_transaction (void)
{
  diskfs_transaction_t *txn;
  journal_t *journal;

  if (journal_thread_depth > 0)
    {
      /* Nested start.  The outer handle already holds a t_active_threads count,
         so this transaction cannot drain until that handle is released.
         It may already be T_LOCKED: commit is waiting for us.  Joining it
         here keeps the whole RPC in one transaction.  No journal lock and
         no t_active_threads change are needed. */
      journal_thread_depth++;
      return journal_thread_txn;
    }

  /* Read once: a pager thread allocating for a write fault starts a
     handle with nothing to keep the journal from stopping meanwhile.  A
     stopped journal is not freed, and keeps j_must_exit set, which the
     join checks again under its lock.  */
  journal = __atomic_load_n (&ext2_journal, __ATOMIC_ACQUIRE);
  if (!journal || journal->j_must_exit)
    return NULL;

  JOURNAL_LOCK (journal);
  txn = journal_join_transaction_locked (journal);
  JOURNAL_UNLOCK (journal);

  if (txn)
    {
      journal_thread_txn = txn;
      journal_thread_depth = 1;
      journal_thread_sync = 0;
    }
  return txn;
}

/* Close one start on this thread.  Returns 1 when TXN was the outermost
   handle and the caller must release the t_active_threads count it holds.  */
static int
journal_thread_release (diskfs_transaction_t *txn)
{
  assert_backtrace (journal_thread_depth > 0);
  assert_backtrace (txn == journal_thread_txn);

  if (--journal_thread_depth > 0)
    return 0;

  journal_thread_txn = NULL;
  return 1;
}

/**
 * Adds a modified filesystem block to the SPECIFIC transaction handle.
 * Defers the actual memory copy until the transaction stops.
 */
error_t
journal_get_write_access (diskfs_transaction_t *txn, block_t fs_blocknr)
{
  error_t err = 0;
  journal_t *journal;
  if (!ext2_journal)
    goto out;

  if (!txn)
    {
      JRNL_LOG_WARN
	("Transaction null so no write access granted for block %u.",
	 fs_blocknr);
      goto out;
    }

  journal = journal_lock_current ();
  if (!journal)
    goto out;
  err = journal_get_write_access_locked (journal, txn, fs_blocknr, 1);
  if (err)
    JRNL_LOG_WARN
      ("Didn't manage to get journal write access for block %u. (%s).",
       fs_blocknr, strerror (err));
  JOURNAL_UNLOCK (journal);

out:
  return err;
}

/* Return the transaction of the calling thread's open handle.

   While the journal is live the caller must hold a handle, taken with
   diskfs_journal_start_transaction; calling without one is a bug and
   asserts.  The result is then never NULL, and the handle's
   t_active_threads count keeps it T_RUNNING or T_LOCKED until the handle
   is released, so blocks may be added to it.

   NULL is returned only when the thread holds no handle because there is
   no journal, or because the journal is shutting down (j_must_exit) and
   diskfs_journal_start_transaction declined to open one.  Callers must
   accept NULL and treat it as "do not journal".

   Under NDEBUG the missing-handle check is compiled out: a caller that
   forgot its handle gets NULL and its edits silently go unjournaled.  */
diskfs_transaction_t *
journal_thread_transaction (void)
{
  diskfs_transaction_t *txn = journal_thread_txn;

  assert_backtrace (!ext2_journal || ext2_journal->j_must_exit
		    || journal_thread_depth > 0);
#ifndef NDEBUG
  /* The handle's t_active_threads count keeps commit from moving the
     transaction past T_LOCKED.  A commit sets T_LOCKED under the journal
     lock, so read the state under it too.  */
  journal_t *journal;
  if (txn && (journal = journal_lock_current ()))
    {
      transaction_state_t state = txn->t_state;

      JOURNAL_UNLOCK (journal);
      assert_backtrace (state == T_RUNNING || state == T_LOCKED);
    }
#endif /* ! NDEBUG */
  return txn;
}

/* Adds FS_BLOCKNR, a metadata block whose live bytes are outside the disk
   cache, to TXN before it is edited.  From here on pager writes of the
   block are intercepted.  The caller supplies the edited image through
   journal_mark_dirty_external, or gives the block up through
   journal_release_access.  */
error_t
journal_get_write_access_external (diskfs_transaction_t *txn,
				   block_t fs_blocknr)
{
  error_t err;
  journal_t *journal;

  if (!txn || !(journal = journal_lock_current ()))
    return 0;

  err = journal_get_write_access_locked (journal, txn, fs_blocknr, 0);
  if (err)
    JRNL_LOG_WARN
      ("Didn't manage to get journal write access for block %u. (%s).",
       fs_blocknr, strerror (err));
  JOURNAL_UNLOCK (journal);
  return err;
}

/* Copies DATA (block_size bytes) into TXN as the image of FS_BLOCKNR,
   which journal_get_write_access_external added to it.  DATA may be a
   mapping of the block, so it is read into the scratch buffer before the
   journal lock is taken: a fault under the lock would re-enter the
   journal through the pager.  Returns EIO when DATA cannot be read.  */
error_t
journal_mark_dirty_external (diskfs_transaction_t *txn, block_t fs_blocknr,
			     const void *data)
{
  journal_buffer_t *jb;
  journal_t *journal;
  error_t err;

  if (!ext2_journal || !txn)
    return 0;

  pthread_mutex_lock (&journal_external_scratch_lock);
  err = hurd_safe_copyin (journal_external_scratch, data, block_size);
  if (err)
    {
      pthread_mutex_unlock (&journal_external_scratch_lock);
      JRNL_LOG_WARN ("Cannot read the image of block %u. (%s).",
		     fs_blocknr, strerror (err));
      return EIO;
    }

  journal = journal_lock_current ();
  if (!journal)
    {
      pthread_mutex_unlock (&journal_external_scratch_lock);
      return 0;
    }
  jb = journal_map_lookup (&txn->t_buffer_map, fs_blocknr);
  if (jb)
    {
      /* Only the committing transaction and the checkpoint list are
	 notified of home writes, and TXN is neither while the caller's
	 handle is open.  */
      assert_backtrace (!jb->jb_is_written);
      /* A pager write during the edit put the page in the shadow, and set
	 needs_flush.  The edited image replaces it, and reads of the block
	 return this image until its home write.  */
      memcpy (jb->jb_shadow_data, journal_external_scratch, block_size);
      jb->needs_copy = 0;
      jb->jb_awaiting_data = 0;
      jb->jb_editing = 0;
    }
  else
    JRNL_LOG_WARN ("mark_dirty_external called on unreserved block %u! "
		   "Missing get_write_access_external?", fs_blocknr);
  JOURNAL_UNLOCK (journal);
  pthread_mutex_unlock (&journal_external_scratch_lock);
  return 0;
}

/* Gives up FS_BLOCKNR when TXN has no image of it yet, so commit does not
   log an empty shadow.  A pager write intercepted since the reservation
   put the page as it is in the shadow and cleared jb_awaiting_data, so
   that image stays in TXN instead.  */
void
journal_release_access (diskfs_transaction_t *txn, block_t fs_blocknr)
{
  journal_buffer_t *jb;
  journal_t *journal;

  if (!txn || !(journal = journal_lock_current ()))
    return;

  jb = journal_map_lookup (&txn->t_buffer_map, fs_blocknr);
  if (jb)
    jb->jb_editing = 0;
  if (!jb || !jb->jb_awaiting_data)
    goto out;
  if (jb->needs_flush)
    {
      /* The page went into the shadow before this reservation, and it
	 is not home yet.  Dropping the buffer would lose it.  */
      jb->jb_awaiting_data = 0;
      goto out;
    }

  /* A buffer the stop-time sweep is copying must not be freed: the sweep
     walks its own list of them unlocked.  The sweep only takes buffers
     with needs_copy set, and an external reservation clears it.  It
     would be set only if TXN had also reserved the block through the
     disk cache, which a directory block never is.  */
  assert_backtrace (!jb->needs_copy);
  journal_map_remove (&txn->t_buffer_map, fs_blocknr);
  if (!jb->jb_is_written && txn->t_outstanding_io > 0)
    txn->t_outstanding_io--;
  journal_logged_unref_locked (journal, fs_blocknr);
  journal_free_buffer (journal, jb);
out:
  JOURNAL_UNLOCK (journal);
}

/* Two flags.  journal_thread_sync tells this thread's RPC tail to commit
   rather than stop.  txn->sync_needed tells the outermost stop to wake
   kjournald when t_active_threads reaches 0, for callers that never commit
   (the pager reaches diskfs_drop_node with diskfs_synchronous set). */
void
diskfs_journal_set_sync (diskfs_transaction_t *txn)
{
  if (!txn)
    return;

  assert_backtrace (txn == journal_thread_txn);
  journal_thread_sync = 1;
  txn->sync_needed = 1;
}

int
diskfs_journal_needs_sync (diskfs_transaction_t *txn)
{
  if (!txn)
    return 0;

  assert_backtrace (txn == journal_thread_txn);
  return journal_thread_sync;
}

/* Helper to reset the header for a new block */
static void
setup_header (void *buf, const diskfs_transaction_t *txn, uint32_t block_type)
{
  journal_header_t *h = (journal_header_t *) buf;
  h->h_magic = htobe32 (JBD2_MAGIC_NUMBER);
  h->h_blocktype = htobe32 (block_type);
  h->h_sequence = htobe32 (txn->t_tid);
}

/**
 * Writes a single Descriptor Block and its corresponding Data Blocks to the journal.
 */
static error_t
journal_write_batch (journal_t *journal, const diskfs_transaction_t *txn,
		     void *descriptor_buf, uint32_t descriptor_loc,
		     size_t batch_start_iter, uint32_t batch_count)
{
  error_t err = journal_write_block (journal, descriptor_loc, descriptor_buf);
  if (err)
    return err;

  size_t data_iter = batch_start_iter;
  for (uint32_t i = 0; i < batch_count; i++)
    {
      journal_buffer_t *p =
	journal_map_iterate (&txn->t_buffer_map, &data_iter);
      uint32_t data_loc = journal_next_log_block_safe (journal);
      void *data = p->jb_shadow_data;

      /* The shadow itself stays as it is: it serves reads while it is
	 written, and after an abort.  */
      if (p->jb_escaped)
	{
	  memcpy (journal->j_escape_buf, data, block_size);
	  memset (journal->j_escape_buf, 0, sizeof (uint32_t));
	  data = journal->j_escape_buf;
	}
      err = journal_write_block (journal, data_loc, data);
      if (err)
	return err;
    }

  return 0;
}

/* Writes the Descriptor Block + All Data Blocks (Escaped) */
static error_t
journal_write_payload (journal_t *journal, const diskfs_transaction_t *txn)
{
  if (txn->t_buffer_map.size == 0)
    return 0;

  void *descriptor_buf = journal->j_descriptor_buf;
  memset (descriptor_buf, 0, block_size);
  setup_header (descriptor_buf, txn, JBD2_DESCRIPTOR_BLOCK);

  uint32_t tag_offset = sizeof (journal_header_t);
  error_t err = 0;
  uint32_t descriptor_loc = journal_next_log_block_safe (journal);

  size_t iter = 0;
  size_t batch_start_iter = 0;
  uint32_t batch_count = 0;
  size_t items_written = 0;
  journal_buffer_t *jb;

  while ((jb = journal_map_iterate (&txn->t_buffer_map, &iter)) != NULL)
    {
      assert_backtrace (!jb->needs_copy);
      assert_backtrace (!jb->jb_awaiting_data);

      /* If the descriptor block is full, flush the current batch first */
      if (tag_offset + sizeof (journal_block_tag_t) > block_size)
	{
	  journal_block_tag_t *prev_tag =
	    (journal_block_tag_t *) ((char *) descriptor_buf + tag_offset -
				     sizeof (journal_block_tag_t));
	  uint32_t prev_flags = be32toh (prev_tag->t_flags);
	  prev_tag->t_flags = htobe32 (prev_flags | JBD2_FLAG_LAST_TAG);

	  JRNL_LOG_DEBUG ("[COMMIT] Writing Interleaved Batch (Desc loc: %u)",
			  descriptor_loc);
	  err =
	    journal_write_batch (journal, txn, descriptor_buf, descriptor_loc,
				 batch_start_iter, batch_count);
	  if (err)
	    return err;

	  /* Prepare for the next batch */
	  descriptor_loc = journal_next_log_block_safe (journal);
	  memset (descriptor_buf, 0, block_size);
	  setup_header (descriptor_buf, txn, JBD2_DESCRIPTOR_BLOCK);
	  tag_offset = sizeof (journal_header_t);
	  batch_start_iter = iter - 1;	/* Point to the item that caused the flush */
	  batch_count = 0;
	}

      /* Add the current block to the Descriptor Block */
      journal_block_tag_t *tag =
	(journal_block_tag_t *) ((char *) descriptor_buf + tag_offset);
      tag->t_blocknr = htobe32 (jb->jb_blocknr);

      items_written++;
      uint32_t flags = JBD2_FLAG_SAME_UUID;

      if (items_written == txn->t_buffer_map.size)
	flags |= JBD2_FLAG_LAST_TAG;

      /* Escaping Logic */
      uint32_t data_head;
      memcpy (&data_head, jb->jb_shadow_data, sizeof (data_head));
      jb->jb_escaped = data_head == htobe32 (JBD2_MAGIC_NUMBER);
      if (jb->jb_escaped)
	flags |= JBD2_FLAG_ESCAPE;

      tag->t_flags = htobe32 (flags);
      tag_offset += sizeof (journal_block_tag_t);
      batch_count++;
    }

  /* Write the Final Batch */
  if (batch_count > 0)
    {
      JRNL_LOG_DEBUG ("[COMMIT] Writing Final Batch (Desc loc: %u)",
		      descriptor_loc);
      err =
	journal_write_batch (journal, txn, descriptor_buf, descriptor_loc,
			     batch_start_iter, batch_count);
    }

  return err;
}

/* Writes TXN's revoke records.  Recovery then skips the copies of those
   blocks in TXN and in older transactions, while a copy in a newer
   transaction, taken after the block was reused, is still replayed.  */
static error_t
journal_write_revoke_records (journal_t *journal,
			      const diskfs_transaction_t *txn)
{
  uint32_t per_block = journal_revokes_per_block ();
  void *buf = journal->j_descriptor_buf;
  journal_revoke_header_t *header = buf;
  uint32_t *records = (uint32_t *) (header + 1);
  uint32_t n = 0;
  size_t iter = 0;
  journal_bucket_t *bucket;

  if (txn->t_revoke_map.size == 0)
    return 0;

  for (;;)
    {
      /* Find the next revoked block.  The set keeps only keys.  */
      bucket = NULL;
      while (iter < txn->t_revoke_map.capacity)
	{
	  journal_bucket_t *b = &txn->t_revoke_map.buckets[iter++];
	  if (b->jb)
	    {
	      bucket = b;
	      break;
	    }
	}

      if (n == per_block || (!bucket && n > 0))
	{
	  header->r_count =
	    htobe32 (sizeof (journal_revoke_header_t) + n * sizeof (uint32_t));
	  error_t err = journal_write_block (journal,
					     journal_next_log_block_safe
					     (journal), buf);
	  if (err)
	    return err;
	  n = 0;
	}
      if (!bucket)
	return 0;

      if (n == 0)
	{
	  memset (buf, 0, block_size);
	  setup_header (buf, txn, JBD2_REVOKE_BLOCK);
	}
      records[n++] = htobe32 (bucket->blocknr);
    }
}

/* Writes the Commit Block */
static error_t
journal_write_commit_record (journal_t *journal,
			     diskfs_transaction_t *txn, uint32_t commit_loc)
{
  void *commit_buf = journal->j_commit_buf;
  memset (commit_buf, 0, block_size);
  setup_header (commit_buf, txn, JBD2_COMMIT_BLOCK);
  return journal_write_block (journal, commit_loc, commit_buf);
}

/**
 * Ensures there is enough free space in the ring buffer to commit the
 * transaction. If space is dangerously low, it forces a synchronous checkpoint.
 * MUST be called with JOURNAL_LOCK held.
 */
static inline void
journal_ensure_commit_space_locked (journal_t *journal,
				    const diskfs_transaction_t *txn)
{
  /* Since we have active checkpointing in place it should be
   * rare that this is needed. Yet it is here as an escape hatch
   * in those rare cases. */
  uint32_t needed =
    txn->t_buffer_map.size +
    (txn->t_buffer_map.size / JRNL_DESCRIPTOR_RATIO) +
    journal_revoke_blocks (txn) + JRNL_COMMIT_MARGIN;
  uint32_t low_water =
    (journal->j_last - journal->j_first) / JRNL_LOW_WATER_RATIO;

  if (journal->j_free < needed || journal->j_free < low_water)
    journal_force_checkpoint_locked (journal);

  /* The forced checkpoint stops at its own target, which a very large
     transaction can exceed.  Empty the log for it.  */
  if (journal->j_free < needed)
    journal_flush_checkpoints_locked (journal, UINT32_MAX, 0);

  /* Nothing bounds the size of one transaction: a truncate frees blocks
     in a single one, and touches a bitmap block for every group it
     frees from.  Writing a transaction larger than the log would wrap
     over its own start and corrupt the journal.  Stop before any of it
     is written: recovery then sees the state before this transaction.  */
  if (journal->j_free < needed)
    {
      JRNL_LOG_WARN ("Transaction %u needs %u log blocks, but the journal "
		     "has only %u.  Use a larger journal.",
		     txn->t_tid, needed, journal->j_free);
      assert_backtrace (journal->j_free >= needed);
    }
}

/* The commit of TXN failed with ERR, partway through writing it to the
   log.  As jbd2 does, stop the journal and make the file system
   read-only: TXN's blocks must not go home without their commit record,
   and nothing after TXN can commit.  kjournald makes the filesystem
   read-only (journal_set_readonly).

   TXN is kept, not freed: its shadows of pages the pager has written
   are the only copy of them.  Reads keep finding them, and pager writes
   of its blocks keep going into them, for as long as the translator
   runs; read-only, diskfs_shutdown does not even stop the journal.  The
   running transaction is never committed either, and keeps its own.
   After a reboot the log replays up to the transaction before TXN, as
   after a crash.  s_errno in the journal superblock has e2fsck check the
   whole file system, and keeps ext2fs from mounting it before then.

   MUST be called with JOURNAL_LOCK held.  */
static void
journal_abort_locked (journal_t *journal, diskfs_transaction_t *txn,
		      error_t err)
{
  journal_superblock_t *jsb = (journal_superblock_t *) journal->j_sb_buffer;

  txn->t_state = T_ABORTED;
  journal->j_aborted_transaction = txn;
  journal->j_aborted_tid = txn->t_tid;
  journal->j_errno = err;
  pthread_cond_signal (&journal->j_flusher_wakeup);

  ext2_error ("commit of transaction %u failed (%s); journal aborted, "
	      "making the file system read-only", txn->t_tid, strerror (err));
  jsb->s_errno = htobe32 ((uint32_t) -err);
  err = journal_write_block (journal, 0, jsb);
  if (err)
    JRNL_LOG_WARN ("Failed to record the abort in the journal superblock: "
		   "%s", strerror (err));
}

/* After an abort, make the filesystem read-only for good: only e2fsck
   can clear the error (journal_check).  As diskfs_set_readonly does, the
   protid RPCs are inhibited meanwhile, so that none is past its check of
   diskfs_readonly when it changes, to fail an assertion that it is
   writable.  Unlike diskfs_set_readonly, nothing is synced and the store
   stays writable (diskfs_readonly_changed): the journal keeps its
   transactions in memory.  The commit that aborted cannot do this: it
   can run in an RPC, or hold the journal lock that RPCs wait for.

   Called by kjournald without the journal lock.  Returns EBUSY while
   someone else inhibits the RPCs (diskfs_set_readonly, diskfs_shutdown),
   and kjournald tries again on its next tick.  */
static error_t
journal_set_readonly (journal_t *journal)
{
  error_t err = ports_inhibit_class_rpcs (diskfs_protid_class);

  if (err)
    return err;
  JOURNAL_LOCK (journal);
  diskfs_hard_readonly = diskfs_readonly = 1;
  JOURNAL_UNLOCK (journal);
  ports_resume_class_rpcs (diskfs_protid_class);
  return 0;
}

int
journal_aborted (void)
{
  return ext2_journal
    && __atomic_load_n (&ext2_journal->j_errno, __ATOMIC_RELAXED) != 0;
}

/* What a commit of transaction TID returns: 0 once it is durable, the
   error that aborted the journal if TID is the aborted transaction or a
   later one.  MUST be called with JOURNAL_LOCK held.  */
static error_t
journal_commit_error_locked (journal_t *journal, uint32_t tid)
{
  if (journal->j_errno && !tid_gt (journal->j_aborted_tid, tid))
    return journal->j_errno;
  return 0;
}

/* Whether TXN has nothing to commit.  Frees alone make a transaction
   worth committing: their revoke records must reach the log, and the
   blocks stay busy until then.  */
static int
journal_txn_empty (diskfs_transaction_t *txn)
{
  if (txn->t_buffer_map.size || txn->t_freed_blocks
      || txn->t_revoke_map.size)
    return 0;
  JRNL_LOG_DEBUG ("Txn %u is empty. Keeping it open.", txn->t_tid);
  return 1;
}

/**
 * Commits the transaction. This function expects
 * journal lock to be held. */
static error_t
journal_commit_running_transaction_locked (journal_t *journal)
{
  error_t err = 0;
  uint32_t commit_loc;
  diskfs_transaction_t *txn;

  txn = journal->j_running_transaction;

  if (!txn || txn->t_state != T_RUNNING)
    {
      err = EINVAL;
      goto out;
    }
  if (journal_txn_empty (txn))
    goto out;

  while (journal->j_committing_transaction != NULL)
    JOURNAL_WAIT (&journal->j_commit_done, journal);

  /* After an abort nothing commits again.  */
  if (journal->j_errno)
    {
      err = journal->j_errno;
      goto out;
    }
  /* The wait dropped the lock: the transaction checked above can have
     been committed by another thread, and its successor is empty.  On
     shutdown, committing that one would put it on the checkpoint list
     after the stop's quiesce cleared it.  */
  txn = journal->j_running_transaction;
  if (!txn || journal_txn_empty (txn))
    goto out;

  journal->j_committing_transaction = txn;

  /* Checkpoint drops the lock.  Do it while txn is still running, so a
     start that sneaks in joins txn and is waited for below.  Other
     committers are already blocked on j_committing_transaction. */
  if (journal->j_free < journal->j_min_free)
    journal_force_checkpoint_locked (journal);

  /* Drain.  No successor exists while txn is T_LOCKED: an outermost start
     on an RPC thread waits in journal_join_transaction_locked, a pager
     thread joins txn.  So every thread that can edit a block in this map
     is counted int_active_threads , and t_active_threads == 0 means the last shadow
     copy taken by diskfs_journal_stop_transaction_locked is final.  The
     successor is published only after that, so no thread in the next
     transaction ever edits a block this one has yet to copy.  This
     thread holds no handle: kjournald never starts one, and
     diskfs_journal_commit_transaction releases its handle first. */
  journal->j_running_transaction = NULL;
  txn->t_state = T_LOCKED;

  while (txn->t_active_threads > 0)
    JOURNAL_WAIT (&journal->j_commit_wait, journal);

  txn->t_state = T_FLUSHING;

  /* Publish the successor before any unlock, then release the starters
     waiting for it.  On shutdown there is no successor; the broadcast
     lets them see j_must_exit and return NULL. */
  if (!journal->j_must_exit
      && !journal_create_running_transaction_locked (journal))
    JRNL_LOG_WARN ("Failed to create the successor transaction.");
  pthread_cond_broadcast (&journal->j_running_wait);

  journal_ensure_commit_space_locked (journal, txn);

  txn->t_log_start = journal_next_block_would_be (journal);
  /* Unlock for I/O.  The successor is j_running_transaction, and this
     transaction is T_FLUSHING with its participants drained.  Its map and
     shadow data are stable. */
  JOURNAL_UNLOCK (journal);

  /* Write Data (I/O) */
  err = journal_write_payload (journal, txn);
  if (err)
    goto abort_commit;

  /* The revoke set is stable: frees are recorded only while TXN is
     T_RUNNING or T_LOCKED.  */
  err = journal_write_revoke_records (journal, txn);
  if (err)
    goto abort_commit;

  /* Ensure Data is on disk */
  flush_to_disk ();

  commit_loc = journal_next_log_block_safe (journal);
  err = journal_write_commit_record (journal, txn, commit_loc);
  if (err)
    goto abort_commit;

  /* Into an empty log, the superblock must have the log's start before
     the commit is durable: from then on TXN's blocks go home, and with
     s_start 0 recovery would replay nothing and leave TXN half there.  */
  JOURNAL_LOCK (journal);
  if (journal->j_disk_tail == 0)
    {
      if (journal->j_tail == 0)
	journal->j_tail = txn->t_log_start;
      err = journal_update_superblock (journal, txn->t_tid,
				       txn->t_log_start);
      if (err)
	{
	  /* Recovery would not see TXN: it must not commit.  */
	  JRNL_LOG_WARN ("Failed to update superblock. %s", strerror (err));
	  JOURNAL_UNLOCK (journal);
	  goto abort_commit;
	}
    }
  JOURNAL_UNLOCK (journal);

  /* Ensure Commit is persistent */
  flush_to_disk ();

  /* The WAL barrier is crossed! Tell the Pager it can write safely!
     The write-hazard path reads the state under the lock.  */
  JOURNAL_LOCK (journal);
  txn->t_state = T_COMMITTED;
  JOURNAL_UNLOCK (journal);
  /* Flush any intercepted VM pager blocks to the primary disk */
  journal_flush_intercepted_payloads (journal, txn);

  /* IO done, lock again and finalize Metadata */
  JOURNAL_LOCK (journal);

  journal->j_last_committed_tid = txn->t_tid;

  txn->t_state = T_FINISHED;
  txn->t_checkpoint_next = NULL;
  journal->j_committing_transaction = NULL;
  journal_freed_extent_t *freed_extents = txn->t_freed_blocks;
  uint32_t freed_tid = txn->t_tid;
  txn->t_freed_blocks = NULL;

  if (journal->j_checkpoint_last)
    journal->j_checkpoint_last->t_checkpoint_next = txn;
  else
    journal->j_checkpoint_list = txn;
  journal->j_checkpoint_last = txn;

  /* Wake up everyone waiting in journal_wait_on_tid */
  pthread_cond_broadcast (&journal->j_commit_done);
  /* Start a background checkpoint now rather than at the next tick.  When
     kjournald is the committer it checks for itself after the commit.  */
  if (journal_bg_checkpoint_needed_locked (journal))
    pthread_cond_signal (&journal->j_flusher_wakeup);
  JOURNAL_UNLOCK (journal);

  journal_forget_freed_blocks (journal, freed_tid, freed_extents);
  JOURNAL_LOCK (journal);
  goto out;
abort_commit:
  /* We hit a physical I/O error. We must clear the pipeline slot and wake
     up any sleeping threads so they don't deadlock.  Their commits fail
     too.  */
  JOURNAL_LOCK (journal);
  journal->j_committing_transaction = NULL;
  journal->j_last_committed_tid = txn->t_tid;
  /* The frees did not commit, and older copies of their blocks were not
     forgotten.  The blocks stay busy until the next mount.  */
  if (tid_gt (txn->t_tid, journal->j_released_tid))
    journal->j_released_tid = txn->t_tid;
  journal_abort_locked (journal, txn, err);
  pthread_cond_broadcast (&journal->j_commit_done);
out:
  return err;
}

/**
 * The background journal thread (kjournald).
 * * Wakes up every 5 seconds to commit the currently running transaction
 * to the journal ring buffer. Crucially, this ONLY writes to the log,
 * not the main filesystem.
 *
 * Purpose:
 * - Data Loss Bound: Caps the maximum window of lost work to 5 seconds
 * (matching standard Linux ext3/ext4 behavior), rather than relying on
 * the 30-second diskfs_sync_everything() interval.
 * - Transaction Sizing: Prevents the in-memory shadow buffer map from
 * growing infinitely large during heavy metadata storms (e.g., compiling).
 * - Pager Optimization: By eagerly committing transactions in the background,
 * we proactively satisfy the Write-Ahead Log (WAL) barrier. This ensures
 * that when the Mach VM Pager eventually needs to flush dirty pages to the
 * main disk, it doesn't stall the system waiting for synchronous journal I/O.
 *
 * - Background Checkpointing: After each wake it checkpoints the oldest
 * committed transactions once the journal is more than half full (see
 * journal_bg_checkpoint_locked), so that commit rarely has to stall on a
 * forced checkpoint.  A pass gives way to a requested commit.
 *
 * It commits on the 5-second tick and when a stop requests it
 * (j_commit_requested).  A wake only for checkpointing does not commit.
 *
 * Future work: The interval could be made dynamic based on VFS load, but
 * a static 5-second interval provides a solid baseline.
 */
static void *
kjournald_thread (void *arg)
{
  journal_t *journal = (journal_t *) arg;
  struct timespec ts;

  JOURNAL_LOCK (journal);
  while (!journal->j_must_exit)
    {
      int commit = journal->j_commit_requested;

      /* A request made while we were busy is not waited for: its signal
	 has already gone.  */
      if (!commit)
	{
	  clock_gettime (CLOCK_MONOTONIC, &ts);
	  ts.tv_sec += 5;

	  int err = pthread_cond_clockwait (&journal->j_flusher_wakeup,
					    &journal->j_state_lock,
					    CLOCK_MONOTONIC, &ts);
	  commit = err == ETIMEDOUT || journal->j_commit_requested;
	}
      journal->j_commit_requested = 0;

      if (journal->j_must_exit)
	break;
      if (journal->j_errno && !diskfs_readonly)
	{
	  JOURNAL_UNLOCK (journal);
	  error_t err = journal_set_readonly (journal);
	  JOURNAL_LOCK (journal);
	  if (err)
	    JRNL_LOG_DEBUG ("Read-only after the abort must wait: %s",
			    strerror (err));
	  continue;
	}
      if (diskfs_readonly)
	continue;

      if (commit && journal->j_running_transaction)
	{
	  JRNL_LOG_DEBUG ("Woke the journal up:\n"
			  " - Sequence: %u\n"
			  " - Start (Head): %u\n"
			  " - First Data Block: %u\n"
			  " - Total Blocks: %u",
			  journal->j_transaction_sequence, journal->j_head,
			  journal->j_first, journal->j_last);

	  error_t err = journal_commit_running_transaction_locked (journal);
	  if (err)
	    JRNL_LOG_WARN ("Background commit failed: %s", strerror (err));
	}

      journal_bg_checkpoint_locked (journal);
      /* A home write moved the tail.  */
      if (!journal->j_must_exit)
	journal_write_tail_locked (journal);
    }
  JOURNAL_UNLOCK (journal);
  return NULL;
}

enum journal_state
journal_check (struct node *journal_inode)
{
  journal_superblock_t *jsb = NULL;
  enum journal_state state = JOURNAL_NEEDS_REPLAY;
  block_t phys = 0;
  size_t amount = 0;
  error_t err;

  err = ext2_getblk (journal_inode, 0, 0, &phys);
  if (!err && phys == 0)
    err = EIO;
  if (!err)
    err = store_read (store,
		      (store_offset_t) phys << log2_dev_blocks_per_fs_block,
		      block_size, (void **) &jsb, &amount);
  if (!err && amount < sizeof (journal_superblock_t))
    err = EIO;

  if (err)
    JRNL_LOG_WARN ("Cannot read the journal superblock: %s", strerror (err));
  else if (be32toh (jsb->s_header[0]) != JBD2_MAGIC_NUMBER
	   || (be32toh (jsb->s_header[1]) != JBD2_SUPERBLOCK_V1
	       && be32toh (jsb->s_header[1]) != JBD2_SUPERBLOCK_V2))
    JRNL_LOG_WARN ("Bad journal superblock.");
  else if (jsb->s_start != 0)
    /* Whatever its features, only e2fsck can replay it.  */
    state = JOURNAL_NEEDS_REPLAY;
  else if (jsb->s_errno != 0)
    /* The journal was aborted (journal_abort_locked).  e2fsck must check
       the whole filesystem first; it clears the error.  */
    state = JOURNAL_NEEDS_REPLAY;
  else if (be32toh (jsb->s_header[1]) == JBD2_SUPERBLOCK_V1)
    /* No feature fields, and so no revoke records.  */
    state = JOURNAL_UNSUPPORTED;
  else if ((be32toh (jsb->s_feature_compat) & ~JBD2_KNOWN_COMPAT_FEATURES)
	   || (be32toh (jsb->s_feature_ro_compat)
	       & ~JBD2_KNOWN_ROCOMPAT_FEATURES)
	   || (be32toh (jsb->s_feature_incompat)
	       & ~JBD2_KNOWN_INCOMPAT_FEATURES))
    state = JOURNAL_UNSUPPORTED;
  else
    state = JOURNAL_USABLE;

  if (jsb)
    munmap (jsb, amount);
  return state;
}

journal_t *
journal_create (struct node *journal_inode)
{
  journal_t *j = calloc (1, sizeof (journal_t));
  if (!j)
    ext2_panic ("Cannot create journal struct.");

  init_map (j, journal_inode);

  /* Take ownership of the inode ref */
  diskfs_nref (journal_inode);

  /* Set generic defaults (Will be overwritten by Superblock read later) */
  j->j_first = 1;		/* Skip SB block by default */
  j->j_last = j->map.total_blocks - 1;
  uint32_t total_len = j->j_last - j->j_first;
  j->j_free = total_len;

  j->j_max_transaction_buffers = total_len / JRNL_MAX_TRANS_RATIO;
  if (j->j_max_transaction_buffers < JRNL_MIN_BATCH_BLOCKS)
    j->j_max_transaction_buffers = JRNL_MIN_BATCH_BLOCKS;
  j->j_min_free = j->j_max_transaction_buffers + JRNL_METADATA_OVERHEAD;
  j->j_descriptor_buf = malloc (block_size);
  j->j_commit_buf = malloc (block_size);
  j->j_escape_buf = malloc (block_size);
  /* Shared by every journal this translator starts.  */
  if (!journal_external_scratch)
    journal_external_scratch = malloc (block_size);
  if (!journal_sweep_scratch)
    journal_sweep_scratch = malloc (block_size);
  if (!j->j_descriptor_buf || !j->j_commit_buf || !j->j_escape_buf
      || !journal_external_scratch
      || !journal_sweep_scratch)
    ext2_panic ("No RAM for commit buffers!");

  if (journal_load_superblock (j) != 0)
    ext2_panic ("[JOURNAL] Failed to load superblock!");

  /* The commits write revoke records, which a journal implementation
     that does not know them must not replay.  journal_check has made
     sure this is a V2 superblock.  */
  journal_superblock_t *jsb = (journal_superblock_t *) j->j_sb_buffer;
  if (!(be32toh (jsb->s_feature_incompat) & JBD2_FEATURE_INCOMPAT_REVOKE))
    {
      jsb->s_feature_incompat =
	htobe32 (be32toh (jsb->s_feature_incompat)
		 | JBD2_FEATURE_INCOMPAT_REVOKE);
      if (journal_write_block (j, 0, jsb))
	ext2_panic ("[JOURNAL] Cannot write the journal superblock!");
      flush_to_disk ();
    }
  j->j_last_committed_tid = j->j_transaction_sequence - 1;
  j->j_released_tid = j->j_last_committed_tid;
  if (journal_map_init (&j->j_logged, 0))
    j->j_logged_lost = 1;
  pthread_cond_init (&j->j_commit_done, NULL);
  pthread_mutex_init (&j->j_state_lock, NULL);
  pthread_cond_init (&j->j_commit_wait, NULL);
  pthread_cond_init (&j->j_flusher_wakeup, NULL);
  pthread_cond_init (&j->j_checkpoint_done, NULL);
  pthread_cond_init (&j->j_tail_written, NULL);
  pthread_cond_init (&j->j_flush_wait, NULL);
  pthread_cond_init (&j->j_running_wait, NULL);

  j->j_must_exit = 0;

  /* The running transaction lives for the whole life of the journal.
     Commit installs the next one before it drops the lock, and start only
     joins.  Publish this one before kjournald runs. */
  JOURNAL_LOCK (j);
  if (!journal_create_running_transaction_locked (j))
    ext2_panic ("Cannot create initial journal transaction.");
  JOURNAL_UNLOCK (j);

  if (pthread_create (&j->j_kjournald, NULL, kjournald_thread, j) != 0)
    JRNL_LOG_WARN ("Failed to create a flusher thread.");
  else
    {
      j->j_kjournald_started = 1;
      JRNL_LOG_DEBUG ("Created flusher thread.");
    }

  j->j_pool_memory =
    calloc (JRNL_MAX_FREE_BUFFERS, sizeof (journal_buffer_t));
  if (!j->j_pool_memory)
    ext2_panic ("[JOURNAL] No RAM for buffer pool!");

  /* Chain the contiguous blocks together into a free list */
  for (int i = 0; i < JRNL_MAX_FREE_BUFFERS - 1; i++)
    j->j_pool_memory[i].jb_next = &j->j_pool_memory[i + 1];

  /* The last block points to NULL, and the head points to block 0 */
  j->j_pool_memory[JRNL_MAX_FREE_BUFFERS - 1].jb_next = NULL;
  j->j_free_buffers = &j->j_pool_memory[0];

  return j;
}

/**
 * Safely marks the journal as clean on disk.
 * MUST only be called after sync_global(1) ensures no pager I/O is in flight,
 * otherwise asynchronous pager notifications will cause a Use-After-Free!
 * Returns 1 if the journal superblock on disk now has an empty log.
 */
static int
journal_quiesce_checkpoints (journal_t *journal)
{
  JRNL_LOG_DEBUG ("Journal in quiesce checkpoints.");
  JOURNAL_LOCK (journal);

  /* Its error is not needed: a failed commit aborts the journal and
     keeps the transaction as j_aborted_transaction, out of the
     checkpoint list.  */
  journal_commit_running_transaction_locked (journal);
  /* Set a 10-second deadline for the active commit to finish. */
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  ts.tv_sec += 10;

  int err = 0;

  /* Wait for any active commit to finish writing to the log */
  while (journal->j_committing_transaction != NULL && err == 0)
    err = pthread_cond_clockwait (&journal->j_commit_done,
				  &journal->j_state_lock, CLOCK_MONOTONIC, &ts);
  if (err)
    {
      /* A handle is still open on the committing transaction, as an RPC
	 stuck waiting for something else would hold it.  Leave the
	 checkpoint list and the log as they are, for a replay at the
	 next mount.  */
      JRNL_LOG_WARN
	("Quiesce timed out! Transaction deadlocked. Leaving journal dirty.");
      JOURNAL_UNLOCK (journal);
      return 0;
    }

  /* Write ALL shadow data from every checkpoint transaction to the main disk. */
  /* Pass UINT32_MAX to guarantee we drain the entire checkpoint list */
  int io_error =
    journal_flush_checkpoints_locked (journal, UINT32_MAX, 0);

  if (io_error)
    {
      JRNL_LOG_WARN ("[QUIESCE] I/O error during final checkpoint. "
		     "Leaving journal dirty for WAL replay.");
      JOURNAL_UNLOCK (journal);
      return 0;
    }

  /* Now safe to clear the list and write s_start = 0 to the JBD2 superblock */
  err = journal_clear_checkpoint_list_locked (journal);

  JOURNAL_UNLOCK (journal);
  return !err;
}

static void
diskfs_journal_stop_transaction_locked (journal_t *journal,
					diskfs_transaction_t *txn)
{
  journal_stop_transaction_locked (journal, txn);

  /* Semi auto-commit?  A full transaction asks for a commit even while
     other threads hold handles: with several pager threads allocating
     blocks at once the count rarely drops to zero, and the transaction
     would grow until the next tick, past what the log can hold.
     kjournald drains it as usual.  */
  if ((txn->t_active_threads == 0 && txn->sync_needed)
      || (txn->t_state == T_RUNNING
	  && txn->t_buffer_map.size >= journal->j_max_transaction_buffers))
    {
      journal->j_commit_requested = 1;
      pthread_cond_signal (&journal->j_flusher_wakeup);
    }
}

/* Ends the caller's participation in the given transaction TXN.
   This informs the journal that the logical operation is complete.
   A nested stop only closes this thread's inner start.  When the outermost
   handle is released and the caller is the final participant (t_active_threads
   reaches 0), a transaction flagged for sync, or one that has reached its
   size limit, wakes kjournald.  This function never waits for the commit:
   the pager reaches it, and a pager thread that waits deadlocks.

   This function consumes TXN. The caller must not use TXN after this call. */
void
diskfs_journal_stop_transaction (diskfs_transaction_t *txn)
{
  if (!txn)
    return;

  if (!journal_thread_release (txn))
    return;

  journal_t *journal = journal_lock_current ();
  if (!journal)
    return;
  diskfs_journal_stop_transaction_locked (journal, txn);
  JOURNAL_UNLOCK (journal);
}

/* Ends the caller's participation in the transaction TXN and strictly
   guarantees a synchronous commit to disk.

   Unlike diskfs_journal_stop_transaction, this function will unconditionally
   block the calling thread until all other participants have finished and the
   transaction is safely written to the physical journal log. This is required
   for top-level VFS RPCs that enforce POSIX fsync/O_SYNC guarantees.

   This function consumes TXN. The caller must not use TXN after this call,
   and it MUST NOT call diskfs_journal_stop_transaction on it.

   Returns 0 once TXN is durable, or the error of a failed commit: of TXN,
   or of an earlier transaction, after which the journal commits nothing
   more.  */
error_t
diskfs_journal_commit_transaction (diskfs_transaction_t *opaque_txn)
{
  error_t err;
  journal_t *journal;

  if (!opaque_txn)
    return 0;

  diskfs_transaction_t *txn = (diskfs_transaction_t *) opaque_txn;
  uint32_t tid = txn->t_tid;

  /* Commit belongs at the end of the RPC, on the outermost handle.  A
     nested commit would wait for t_active_threads to drain while this thread's
     outer handle holds a count: a self-deadlock.  Catch it here. */
  int outermost = journal_thread_release (txn);
  assert_backtrace (outermost);

  JRNL_LOG_DEBUG ("Committing tx id: %u.", txn->t_tid);

  /* Once the handle is released below, the stop no longer waits for
     this thread and can clear ext2_journal while the waits drop the
     lock: use the journal read here throughout.  */
  journal = journal_lock_current ();
  if (!journal)
    return 0;

  /* Decrement the refcount while holding the lock. */
  journal_stop_transaction_locked (journal, txn);

  if (txn->t_buffer_map.size == 0)
    JRNL_LOG_DEBUG ("In diskfs_commit, something with no buffers. txn Id: %u",
		    tid);
  /* Check if the transaction is currently RUNNING.
     If it is, We "steal" it and become the committer. */
  if (IS_RUNNING_TID (journal, tid))
    {
      err = journal_commit_running_transaction_locked (journal);
      if (err && !journal->j_errno)
	JRNL_LOG_WARN
	  ("Commit failed in diskfs_journal_commit_transaction: %s",
	   strerror (err));
      /* An empty transaction stays open without waiting, while the
	 caller's earlier changes can be in the one kjournald is
	 committing.  The caller asked for them to be durable too.  */
      journal_wait_on_tid_locked (journal, tid - 1);
      goto out;
    }
  /* We missed it. Someone else (kjournald) stole it.
     We will wait for them to finish here. */
  journal_wait_on_tid_locked (journal, tid);
out:
  err = journal_commit_error_locked (journal, tid);
  JOURNAL_UNLOCK (journal);
  return err;
}

/**
 * Checks a block against active transactions and handles deadlock hazards.
 * Returns 1 if the block was intercepted, 0 if it should be written to
 * physical disk.
 * MUST be called with JOURNAL_LOCK held.
 */
static int
journal_handle_write_hazard_locked (journal_t *journal, block_t b, char *b_data, int disk_cache,
				    int resident)
{
  int intercepted = 0;
  diskfs_transaction_t *commit;
  diskfs_transaction_t *run;
  journal_buffer_t *jb_run;
  journal_buffer_t *jb_commit;
  journal_buffer_t *jb_aborted;
  diskfs_transaction_t *chk;
  diskfs_transaction_t *aborted = journal->j_aborted_transaction;

retry:
  commit = journal->j_committing_transaction;
  run = journal->j_running_transaction;

  jb_run = run ? journal_map_lookup (&run->t_buffer_map, b) : NULL;
  jb_commit = commit ? journal_map_lookup (&commit->t_buffer_map, b) : NULL;
  /* An aborted transaction's blocks never go home: the page goes into
     its shadow, which serves reads.  */
  jb_aborted = aborted ? journal_map_lookup (&aborted->t_buffer_map, b)
    : NULL;

  /* If it's in the committing transaction BUT the WAL barrier is crossed,
     it is safe to write to disk. Remove it from the intercept hazard list. */
  if (commit && commit->t_state == T_COMMITTED && jb_commit)
    jb_commit = NULL;

  /* The block is in a transaction that has not committed, so its home
     write must wait for the commit.  The page goes into the shadow of
     the newest transaction holding the block, which serves reads until
     the flush after that commit writes it home.  No checkpoint is
     touched.

     A sync leaves the page RESIDENT and writes a copy the kernel took
     when it was asked, which can be older than the shadow: an edit
     reserved after that is already swept into it.  The page itself is
     then dirty again, so any newer bytes reach us later.  With every
     reservation swept, such a copy only defers the home write, so that
     a read after a clean eviction gets the shadow.  While a sweep is
     due or running, the copy can hold an edit the shadow lacks, and the
     page can be evicted clean before the sweep reads it: the copy then
     goes into the shadow, which the sweep reads back.  An older copy is
     harmless there, since the page is dirty and the sweep reads it.  */
  if (jb_run || jb_commit || jb_aborted)
    {
      journal_buffer_t *jb = jb_run ? jb_run : jb_commit ? jb_commit
	: jb_aborted;
      int stale = disk_cache && ((jb_run && jb_run->jb_external)
				 || (jb_commit && jb_commit->jb_external)
				 || (jb_aborted && jb_aborted->jb_external));

      /* The disk cache writing a directory block that a transaction holds
	 writes its stale copy: the live bytes are in the directory's file
	 pager.  It must neither replace the shadow nor go home.  */
      assert_backtrace (!stale);
      if (stale)
	{
	  JRNL_LOG_WARN ("Disk cache wrote directory block %u; dropped.", b);
	  return 1;
	}

      if (resident && jb->needs_copy)
	/* The sweep reads the page later, or the shadow if it is gone.  */
	memcpy (jb->jb_shadow_data, b_data, block_size);
      else if (resident && jb->jb_sweeping)
	/* Not raced: no edit is made during a sweep, and if the page is
	   gone, its read waits for this write and gets these bytes.  */
	memcpy (jb->jb_shadow_data, b_data, block_size);
      else if (resident && !jb->jb_awaiting_data && !jb->jb_editing)
	{
	  /* An unreserved edit cannot be told apart from an old copy
	     here; it shows up when the page is evicted.  */
	  if (memcmp (jb->jb_shadow_data, b_data, block_size) != 0)
	    {
	      JRNL_LOG_DEBUG ("Sync of block %u wrote a copy that differs "
			      "from its shadow; kept the shadow.", b);
	    }
	}
      else if (!jb_run && !jb_commit)
	/* Aborted.  It drained before its commit, and an edit since is
	   reserved in the running transaction: the page is the newer.  */
	memcpy (jb->jb_shadow_data, b_data, block_size);
      else if (!jb_run && commit->t_state == T_FLUSHING)
	{
	  /* The commit is writing this shadow to the log, so it must not
	     change.  The page differs from it only by an edit made since
	     the drain without a reservation in the running transaction,
	     which every edit should take before it writes the block.
	     That edit belongs to the running transaction: reserve the
	     block there, and keep the page in its shadow.  */
	  if (memcmp (jb->jb_shadow_data, b_data, block_size) != 0)
	    {
	      static unsigned warnings;
	      int external = jb->jb_external;

	      jb_run = NULL;
	      if (run && !journal_get_write_access_locked (journal, run, b, !external))
		jb_run = journal_map_lookup (&run->t_buffer_map, b);
	      if (jb_run)
		{
		  jb = jb_run;
		  memcpy (jb->jb_shadow_data, b_data, block_size);
		  /* Nothing newer is in the disk cache.  The editor's own
		     reservation sets needs_copy again.  */
		  jb->needs_copy = 0;
		  jb->jb_awaiting_data = 0;
		  if (warnings < 16)
		    {
		      warnings++;
		      JRNL_LOG_WARN ("Block %u changed without a reservation "
				     "while its transaction commits; it goes "
				     "into the next one.", b);
		    }
		}
	      else
		JRNL_LOG_WARN ("Block %u changed without a reservation while "
			       "its transaction commits, and cannot be "
			       "reserved in the next; the change is lost.", b);
	    }
	}
      else
	{
	  /* The transaction still takes edits (T_RUNNING, or T_LOCKED
	     while it drains), and the page holds its latest bytes, newer
	     than the last sweep's copy.  The commit states change under
	     the journal lock, held here.  */
	  memcpy (jb->jb_shadow_data, b_data, block_size);
	  if (jb->jb_sweeping)
	    jb->jb_sweep_raced = 1;
	  /* For a directory block reserved and not yet marked, the page
	     is the image that commits if the reservation is given up.  */
	  jb->jb_awaiting_data = 0;
	}
      jb->needs_flush = 1;

      intercepted = 1;
      JRNL_LOG_DEBUG ("Intercepted rushed pager write for block %u", b);
      return intercepted;
    }

  /* We are definitively going to physical disk.
     Now safely check for hardware races against Checkpoint/Committing threads. */
  if (commit && commit->t_state == T_COMMITTED)
    {
      journal_buffer_t *jb_com_real =
	journal_map_lookup (&commit->t_buffer_map, b);
      if (jb_com_real)
	{
	  if (jb_com_real->jb_is_flushing)
	    {
	      JOURNAL_WAIT (&journal->j_flush_wait, journal);
	      goto retry;
	    }
	  else if (!jb_com_real->jb_is_written)
	    jb_com_real->jb_is_flushing = 1;	/* Claim it for the Pager */
	}
    }

  chk = journal->j_checkpoint_list;
  while (chk)
    {
      journal_buffer_t *jb_chk = journal_map_lookup (&chk->t_buffer_map, b);
      if (jb_chk)
	{
	  if (jb_chk->jb_is_flushing)
	    {
	      JOURNAL_WAIT (&journal->j_flush_wait, journal);
	      goto retry;
	    }
	  else if (!jb_chk->jb_is_written)
	    {
	      /* Claim the buffer so the Checkpoint thread yields if it wakes up. */
	      jb_chk->jb_is_flushing = 1;
	    }
	}
      chk = chk->t_checkpoint_next;
    }

  return 0;
}

/**
 * Checks if a block is safe to coalesce into a physical batch write.
 * If it is safe, it preemptively claims the block in the checkpoint lists
 * so that background checkpoint threads yield to the Pager.
 * MUST be called with JOURNAL_LOCK held.
 */
static int
journal_claim_safe_block_locked (journal_t *journal, block_t b)
{
  diskfs_transaction_t *commit = journal->j_committing_transaction;
  diskfs_transaction_t *run = journal->j_running_transaction;

  journal_buffer_t *jb_run =
    run ? journal_map_lookup (&run->t_buffer_map, b) : NULL;
  journal_buffer_t *jb_commit =
    commit ? journal_map_lookup (&commit->t_buffer_map, b) : NULL;

  /* If commit crossed WAL barrier, it's safe to coalesce and write */
  if (commit && commit->t_state == T_COMMITTED)
    jb_commit = NULL;

  /* If it is an active hazard, we cannot claim it for physical coalescing */
  if (jb_run || jb_commit)
    return 0;
  diskfs_transaction_t *aborted = journal->j_aborted_transaction;
  if (aborted && journal_map_lookup (&aborted->t_buffer_map, b))
    return 0;

  /* Stop coalescing if ANY thread is actively flushing this block to disk */
  journal_buffer_t *jb_commit_flush =
    commit ? journal_map_lookup (&commit->t_buffer_map, b) : NULL;
  if (jb_commit_flush && jb_commit_flush->jb_is_flushing)
    return 0;

  diskfs_transaction_t *chk = journal->j_checkpoint_list;
  while (chk)
    {
      journal_buffer_t *jb_chk = journal_map_lookup (&chk->t_buffer_map, b);
      if (jb_chk && jb_chk->jb_is_flushing)
	return 0;
      chk = chk->t_checkpoint_next;
    }

  /* It is safe to write to disk. CLAIM IT in the lists! */
  if (commit && commit->t_state == T_COMMITTED)
    {
      if (jb_commit_flush && !jb_commit_flush->jb_is_written)
	jb_commit_flush->jb_is_flushing = 1;
    }

  chk = journal->j_checkpoint_list;
  while (chk)
    {
      journal_buffer_t *jb_chk = journal_map_lookup (&chk->t_buffer_map, b);
      if (jb_chk && !jb_chk->jb_is_written)
	jb_chk->jb_is_flushing = 1;
      chk = chk->t_checkpoint_next;
    }

  return 1;
}

static void
journal_clear_flushing_locked (journal_t *journal, block_t start_block, size_t n_blocks)
{
  for (size_t i = 0; i < n_blocks; i++)
    {
      block_t b = start_block + i;
      diskfs_transaction_t *txn = journal->j_checkpoint_list;
      while (txn)
	{
	  journal_buffer_t *jb = journal_map_lookup (&txn->t_buffer_map, b);
	  if (jb && jb->jb_is_flushing)
	    {
	      jb->jb_is_flushing = 0;
	      pthread_cond_broadcast (&journal->j_flush_wait);
	    }
	  txn = txn->t_checkpoint_next;
	}

      txn = journal->j_committing_transaction;
      if (txn)
	{
	  journal_buffer_t *jb = journal_map_lookup (&txn->t_buffer_map, b);
	  if (jb && jb->jb_is_flushing)
	    {
	      jb->jb_is_flushing = 0;
	      pthread_cond_broadcast (&journal->j_flush_wait);
	    }
	}
    }
}

/* A pager's home write of N_BLOCKS blocks from START_BLOCK failed, and
   the pages it wrote are gone.  It wrote them home only because no
   transaction still edited them: the newest copy the journal holds of
   each, committed and not home yet, is what the page held.  Reads must
   return it until a home write brings it there (journal_store_read),
   which the checkpoint does.  */
static void
journal_keep_failed_blocks_locked (journal_t *journal, block_t start_block, size_t n_blocks)
{
  diskfs_transaction_t *commit = journal->j_committing_transaction;

  for (size_t i = 0; i < n_blocks; i++)
    {
      block_t b = start_block + i;
      journal_buffer_t *newest =
	commit ? journal_map_lookup (&commit->t_buffer_map, b) : NULL;

      if (!newest)
	for (diskfs_transaction_t *txn = journal->j_checkpoint_list;
	     txn; txn = txn->t_checkpoint_next)
	  {
	    journal_buffer_t *jb = journal_map_lookup (&txn->t_buffer_map, b);
	    if (jb)
	      newest = jb;
	  }
      if (newest && !newest->jb_is_written)
	newest->needs_flush = 1;
    }
}

/**
 * A block device filter layer for the VFS pager's write path.
 * Replaces raw store_write calls to safely intercept deadlock hazards.
 * When the pager attempts to write a block that is actively locked by a
 * running or committing transaction (rushing the transaction commit cycle),
 * this function redirects the payload into temporary storage (the intercept
 * cache). This preserves the Write-Ahead Log (WAL) ordering and prevents
 * the VM pager from deadlocking the filesystem. Safe blocks are coalesced
 * and written normally to disk.
 */
error_t
journal_store_write (block_t start_block, size_t length, void *buf,
		     size_t *amount, int disk_cache)
{
  /* One journal throughout, as in journal_store_read.  The journal can
     stop while this write is in flight, when no transaction holds its
     blocks: ext2_journal then goes NULL, under the journal's lock.  */
  journal_t *journal = __atomic_load_n (&ext2_journal, __ATOMIC_ACQUIRE);
  if (!journal || length == 0)
    {
      /* Journal is disabled. Passthrough directly to the disk. */
      store_offset_t dev_block =
	(store_offset_t) start_block << log2_dev_blocks_per_fs_block;
      return store_write (store, dev_block, buf, length, amount);
    }
  assert_backtrace (length % block_size == 0);
  size_t n_blocks = length >> log2_block_size;
  error_t final_err = 0;
  char *in_ptr = (char *) buf;
  size_t i = 0;
  size_t total_written = 0;
  int resident = pager_write_keeps_page ();
  /* A write of this run failed: the pager takes the rest of it as failed
     too, and their pages are lost.  */
  int failed = 0;

  JOURNAL_LOCK (journal);
  while (i < n_blocks)
    {
      block_t b = start_block + i;
      char *b_data = in_ptr + (i << log2_block_size);

      if (ext2_journal != journal)
	{
	  /* It stopped, with nothing left in it: the rest goes home as
	     without a journal.  */
	  size_t rest_amount = 0;
	  JOURNAL_UNLOCK (journal);
	  error_t err =
	    store_write (store,
			 (store_offset_t) b << log2_dev_blocks_per_fs_block,
			 b_data, (n_blocks - i) << log2_block_size,
			 &rest_amount);
	  JOURNAL_LOCK (journal);
	  total_written += rest_amount;
	  if (err && !final_err)
	    final_err = err;
	  break;
	}

      int intercepted =
	journal_handle_write_hazard_locked (journal, b, b_data, disk_cache, resident);

      if (intercepted)
	{
	  /* We successfully handled this block in RAM. Move to the next. */
	  JRNL_LOG_DEBUG ("Intercepted hazard for block %u", b);
	  i++;
	  if (!failed)
	    total_written += block_size;
	}
      else if (failed)
	{
	  /* Nothing more goes home: the copy the journal has is the
	     block's, and the hazard check may have claimed it.  */
	  journal_clear_flushing_locked (journal, b, 1);
	  journal_keep_failed_blocks_locked (journal, b, 1);
	  i++;
	}
      else
	{
	  /* Disk I/O Path: Coalesce contiguous safe blocks. */
	  size_t flush_count = 1;

	  /* Lookahead loop: Scan forward to see how many blocks we can safely write together */
	  while (i + flush_count < n_blocks)
	    {
	      block_t next_b = start_block + i + flush_count;

	      if (!journal_claim_safe_block_locked (journal, next_b))
		break;

	      flush_count++;
	    }
	  /* Execute the coalesced physical write OUTSIDE the lock to keep VFS unblocked */
	  store_offset_t dev_block =
	    (store_offset_t) b << log2_dev_blocks_per_fs_block;
	  size_t write_len = flush_count << log2_block_size;
	  size_t chunk_amount = 0;
	  JOURNAL_UNLOCK (journal);
	  error_t err =
	    store_write (store, dev_block, b_data, write_len, &chunk_amount);
	  JOURNAL_LOCK (journal);
	  if (err && !final_err)
	    final_err = err;

	  /* Convert bytes actually written back into full blocks */
	  size_t actual_blocks = chunk_amount >> log2_block_size;

	  /* A stopped journal waited for every claim, so this write had
	     none, and there is nothing to tell it.  */
	  if (actual_blocks > 0 && ext2_journal == journal)
	    journal_notify_blocks_written_locked (journal, b, actual_blocks);

	  if (actual_blocks < flush_count && ext2_journal == journal)
	    {
	      journal_clear_flushing_locked (journal, b + actual_blocks,
					     flush_count - actual_blocks);
	      journal_keep_failed_blocks_locked (journal, b + actual_blocks,
						 flush_count - actual_blocks);
	    }

	  total_written += chunk_amount;
	  i += actual_blocks;

	  /* An error or a short write (e.g., EOF on the device): nothing
	     more of the run goes home, but its blocks a transaction holds
	     still go into their shadows, as the pages are lost.  */
	  if (err || actual_blocks < flush_count)
	    {
	      if (!err && !final_err)
		final_err = ENOSPC;	/* It is No Space Left */
	      failed = 1;
	      i += flush_count - actual_blocks;
	    }
	}
    }
  if (amount)
    *amount = total_written;

  JOURNAL_UNLOCK (journal);
  return final_err;
}

/* The journal buffer whose shadow a read of block B must return: the
   newest one whose home write is deferred (needs_flush).  The running
   transaction's comes first; the committing one's holds the page only
   if it was written out before the running transaction reserved the
   block, and then nothing newer exists.  The flush after a commit runs
   while the transaction is still the committing one, so no other
   transaction has the flag set.  NULL if the home copy is current.
   MUST be called with JOURNAL_LOCK held.  */
static journal_buffer_t *
journal_deferred_source_locked (journal_t *journal, block_t b)
{
  diskfs_transaction_t *run = journal->j_running_transaction;
  diskfs_transaction_t *commit = journal->j_committing_transaction;
  journal_buffer_t *jb;

  jb = run ? journal_map_lookup (&run->t_buffer_map, b) : NULL;
  if (jb && jb->needs_flush)
    return jb;
  jb = commit ? journal_map_lookup (&commit->t_buffer_map, b) : NULL;
  if (jb && jb->needs_flush)
    return jb;

  /* An aborted commit's shadow is newer than any checkpointed one.  */
  diskfs_transaction_t *aborted = journal->j_aborted_transaction;
  jb = aborted ? journal_map_lookup (&aborted->t_buffer_map, b) : NULL;
  if (jb && jb->needs_flush)
    return jb;

  /* A flush after a commit that failed leaves the flag set in a
     transaction that has gone on to the checkpoint list, which writes
     the shadow home later.  Until then the shadow is still the block's
     latest image: the newest such transaction's.  */
  journal_buffer_t *found = NULL;
  for (diskfs_transaction_t *t = journal->j_checkpoint_list; t;
       t = t->t_checkpoint_next)
    {
      jb = journal_map_lookup (&t->t_buffer_map, b);
      if (jb && jb->needs_flush)
	found = jb;
    }
  return found;
}

/* Read LEN bytes of block B from its home location into DST.  */
static error_t
journal_reread_block (block_t b, char *dst, size_t len)
{
  store_offset_t dev_block = (store_offset_t) b << log2_dev_blocks_per_fs_block;
  void *data = dst;
  size_t amount = len;
  error_t err = store_read (store, dev_block, len, &data, &amount);

  if (err)
    return err;
  if (data != dst)
    {
      /* The store returned its own memory.  */
      memcpy (dst, data, amount < len ? amount : len);
      vm_deallocate (mach_task_self (), (vm_address_t) data, amount);
    }
  return amount < len ? EIO : 0;
}

/* Replace, in BUF, just read from disk, the blocks whose home write is
   deferred with their shadows.  Handles a short read at the end of the
   device.

   DEFERRED tells whether some block of the range had its home write
   deferred when the disk read started.  Its flush may have finished
   since, and the read may have fetched the block before the write, so
   the blocks found without the flag now are read again.  The flag is
   only cleared after the home write returned, so the new read sees it.
   A block cannot get the flag during the read: the pager does not write
   a page out while it is paging it in.  */
static error_t
journal_overlay_deferred (journal_t *journal, block_t start_block,
			  size_t length, void *buf, int deferred)
{
  error_t err = 0;

  if (length == 0 || !buf)
    return 0;

  /* Calculate total blocks touched by this read (ceiling division) */
  size_t n_blocks = (length + block_size - 1) >> log2_block_size;

  char *out_ptr = (char *) buf;

  JOURNAL_LOCK (journal);
  for (size_t i = 0; i < n_blocks && !err; i++)
    {
      block_t b = start_block + i;
      /* Calculate exact offset and bounds for this specific block */
      size_t offset = i << log2_block_size;
      size_t copy_len = block_size;
      /* If this is the final, partially-read block, clamp the copy length */
      if (length - offset < block_size)
	copy_len = length - offset;

      journal_buffer_t *jb = journal_deferred_source_locked (journal, b);
      if (jb)
	{
	  memcpy (out_ptr + offset, jb->jb_shadow_data, copy_len);
	  JRNL_LOG_DEBUG ("Read block %u from its shadow (%zu bytes)",
			  b, copy_len);
	  continue;
	}
      if (!deferred)
	continue;

      JRNL_LOG_DEBUG ("Read of block %u may have overlapped its deferred "
		      "home write; reading it again", b);
      JOURNAL_UNLOCK (journal);
      err = journal_reread_block (b, out_ptr + offset, copy_len);
      JOURNAL_LOCK (journal);
    }
  JOURNAL_UNLOCK (journal);
  return err;
}

/**
 * A block device filter layer for the VFS pager's read path.
 * Reads from the disk, and returns the shadow instead for any block whose
 * pager write the journal took in before its transaction committed: the
 * home copy of such a block is stale until the flush after the commit.
 */
error_t
journal_store_read (block_t start_block, size_t length, void **buf,
		    size_t *read_amount)
{
  store_offset_t dev_block =
    (store_offset_t) start_block << log2_dev_blocks_per_fs_block;
  /* Use one journal throughout: a journal_t stays valid after the
     journal stops, so the overlay can still lock it.  The acquire pairs
     with the release that publishes it (ext2_start_journal), so that its
     lock is seen initialized.  */
  journal_t *journal = __atomic_load_n (&ext2_journal, __ATOMIC_ACQUIRE);
  int deferred = 0;

  if (journal)
    {
      size_t n_blocks = (length + block_size - 1) >> log2_block_size;

      JOURNAL_LOCK (journal);
      for (size_t i = 0; i < n_blocks && !deferred; i++)
	deferred = journal_deferred_source_locked (journal,
						   start_block + i) != NULL;
      JOURNAL_UNLOCK (journal);
    }

  error_t err = store_read (store, dev_block, length, buf, read_amount);
  if (!err && journal && *read_amount > 0)
    /* Pass the actual amount read, just in case it was a short read */
    err = journal_overlay_deferred (journal, start_block, *read_amount,
				    *buf, deferred);

  return err;
}

/* Wake kjournald, which saw j_must_exit or will when it wakes, and wait
   for it to end.  */
static void
journal_stop_kjournald (journal_t *journal)
{
  if (!journal->j_kjournald_started)
    return;
  JOURNAL_LOCK (journal);
  pthread_cond_signal (&journal->j_flusher_wakeup);
  JOURNAL_UNLOCK (journal);
  pthread_join (journal->j_kjournald, NULL);
  journal->j_kjournald_started = 0;
}

/* Free what JOURNAL holds, once journal_quiesce_checkpoints has emptied
   it and ext2_journal no longer points to it.  The journal_t itself, with
   its lock and condition variables, is kept: a pager thread may have read
   ext2_journal just before it was cleared and still take the lock.  With
   no transaction left, such a thread finds nothing to do, and everything
   freed here is only reached through a transaction or by the commit,
   checkpoint and superblock code, which no longer runs.  */
static void
journal_free_resources (journal_t *journal)
{
  struct node *jnode = journal->map.inode;

  journal_stop_kjournald (journal);

  JOURNAL_LOCK (journal);

  /* Quiescing committed everything and emptied the checkpoint list, and
     writing the superblock released the retired transactions.  Only an
     empty running transaction can be left, which a commit keeps open.  */
  diskfs_transaction_t *running = journal->j_running_transaction;
  if (journal->j_committing_transaction || journal->j_checkpoint_list
      || journal->j_retired_list
      || (running && (running->t_buffer_map.size || running->t_freed_blocks
		      || running->t_revoke_map.size)))
    {
      JRNL_LOG_WARN ("Journal stopped with transactions left; "
		     "not freeing it.");
      JOURNAL_UNLOCK (journal);
      return;
    }
  journal->j_running_transaction = NULL;
  journal_free_transaction (journal, running);

  journal_map_destroy (&journal->j_logged);
  /* Every buffer is back in the pool.  */
  free (journal->j_pool_memory);
  journal->j_pool_memory = NULL;
  journal->j_free_buffers = NULL;
  free (journal->j_sb_buffer);
  journal->j_sb_buffer = NULL;
  free (journal->j_descriptor_buf);
  journal->j_descriptor_buf = NULL;
  free (journal->j_commit_buf);
  journal->j_commit_buf = NULL;
  free (journal->j_escape_buf);
  journal->j_escape_buf = NULL;
  free (journal->map.phys_blocks);
  journal->map.phys_blocks = NULL;
  journal->map.inode = NULL;

  JOURNAL_UNLOCK (journal);

  /* The reference journal_create took.  */
  if (jnode)
    diskfs_nrele (jnode);
}

void
diskfs_journal_shutdown (void)
{
  /* Only the stop clears ext2_journal, so it stays JOURNAL here.  */
  journal_t *journal = ext2_journal;
  if (!journal)
    return;

  JOURNAL_LOCK (journal);
  /* No new handle from here on; kjournald exits when it wakes.  */
  journal->j_must_exit = 1;
  pthread_cond_broadcast (&journal->j_running_wait); /* Wake waiting starters */
  JOURNAL_UNLOCK (journal);

  /* Sync the disk pager to ensure all shadow data is on disk.  */
  sync_global (1);

  /* Checkpoint all remaining transactions and mark the journal clean.  */
  int empty = journal_quiesce_checkpoints (journal);

  /* Under the lock: a pager write that holds it and still finds the
     journal current can finish with it (journal_store_write).  */
  JOURNAL_LOCK (journal);
  __atomic_store_n (&ext2_journal, NULL, __ATOMIC_RELEASE);
  JOURNAL_UNLOCK (journal);

  /* Final hardware flush.  It also puts the empty journal superblock on
     disk before the needs_recovery flag is cleared.  */
  flush_to_disk ();

  /* Left set when the log is not empty, so that e2fsck replays it.
     The journal is then kept too, as its transactions are; kjournald
     ends either way.  */
  if (empty)
    {
      ext2_clear_needs_recovery ();
      journal_free_resources (journal);
    }
  else
    journal_stop_kjournald (journal);
}
