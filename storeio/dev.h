/* store `device' I/O

   Copyright (C) 1995,96,97,99,2000,2001,2026 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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

#ifndef __DEV_H__
#define __DEV_H__

#include <mach.h>
#include <device/device.h>
#include <pthread.h>
#include <hurd/netfs.h>
#include <stdio.h>
#include <unistd.h>

extern mach_port_t underlying_node;

/* Information about backend store, which we presumptively call a "device".  */
struct dev
{
  /* The device to which we're doing io.  This is null when the
     device is closed, in which case we will open from `store_name'.  */
  struct store *store;

  /* The number of active opens.  */
  int nperopens;

  /* This lock protects `store' and 'nperopens'.  The other members never
     change after creation, except for those locked by io_lock (below).  */
  pthread_mutex_t lock;

  /* A bitmask corresponding to the part of an offset that lies within a
     device block.  */
  unsigned block_mask;

  /* Lock to arbitrate I/O through this device.  Block I/O can occur in
     parallel, and requires only a reader-lock.
     Non-block I/O is always serialized, and requires a writer-lock.  */
  pthread_rwlock_t io_lock;

  /* Non-block I/O is buffered through BUF.  BUF_OFFS is the device offset
     corresponding to the start of BUF (which holds one block); if it is -1,
     then BUF is inactive.  */
  void *buf;
  off_t buf_offs;
  int buf_dirty;

  struct pager *pager;
  pthread_mutex_t pager_lock;
};

struct netnode
{
  struct dev *dev;
  char *name;
  struct node **entries;

  /* If the storage is a disk, this is the number of partitions on it.  */
  size_t entries_num;
};

struct storeio_stat
{
  /* The argument specification that we use to open the store.  */
  struct store_parsed *store_name;
  volatile struct mapped_time_value *current_time;
  pid_t pid;
  uid_t uid;
  gid_t gid;
  mode_t mode;
  int readonly; /* Nonzero if user gave --readonly flag.  */
  int enforced; /* Nonzero if user gave --enforced flag.  */
  int no_fileio; /* Nonzero if user gave --no-fileio flag.  */
  dev_t rdev; /* A unixy device number for st_rdev.  */

  /* Nonzero iff the --no-cache flag was given.
     If this is set, the remaining members are not used at all
     and don't need to be initialized or cleaned up.  */
  int inhibit_cache;
};

extern struct storeio_stat storeio_stat;

error_t create_node (struct node **node, char *name, struct node *dir);
error_t check_dev (struct node *node, struct store *store, int flags);
error_t create_partitions (void);

static inline int
dev_is_readonly (const struct dev *dev)
{
  return storeio_stat.readonly || (dev->store && (dev->store->flags
                                                  & STORE_READONLY));
}

/* Called with DEV->lock held.  Initialize the DEV struct.  */
error_t dev_open_from_store (struct dev *dev, struct store *store);

/* Try to open the store underlying DEV.  */
error_t dev_open (struct dev *dev, struct store_parsed *store_name);

/* Shut down the store underlying DEV and free any resources it consumes.
   DEV itself remains intact so that dev_open can be called again.
   This should be called with DEV->lock held.  */
void dev_close (struct dev *dev);

/* Returns in MEMOBJ the port for a memory object backed by the storage on
   DEV.  Returns 0 or the error code if an error occurred.  */
error_t dev_get_memory_object(struct dev *dev, vm_prot_t prot,
			      memory_object_t *memobj);

/* Try to stop all paging activity, returning true if we were successful.
   If NOSYNC is true, then we won't write back any (kernel) cached pages to
   the device.  */
int dev_stop_paging (int nosync);

/* Try and write out any pending writes to DEV.  If WAIT is true, will wait
   for any paging activity to cease.  */
error_t dev_sync (struct dev *dev, int wait);

/* Write LEN bytes from BUF to DEV, returning the amount actually written in
   AMOUNT.  If successful, 0 is returned, otherwise an error code is
   returned.  */
error_t dev_write (struct dev *dev, off_t offs, const void *buf, size_t len,
		   size_t *amount);

/* Read up to AMOUNT bytes from DEV, returned in BUF and LEN in the with the
   usual mach memory result semantics.  If successful, 0 is returned,
   otherwise an error code is returned.  */
error_t dev_read (struct dev *dev, off_t offs, size_t amount,
		  void **buf, size_t *len);

#ifdef DEBUG
extern FILE *debug_file;
extern pthread_mutex_t debug_lock;
# define debug(format, ...)                             \
  do                                                    \
    {                                                   \
      if (debug_file)                                   \
        {                                               \
          pthread_mutex_lock (&debug_lock);             \
          fprintf (debug_file, format, ## __VA_ARGS__); \
          pthread_mutex_unlock (&debug_lock);           \
        }                                               \
    }                                                   \
  while (0)
#else
# define debug(format, ...) do {} while (0)
#endif

#endif /* !__DEV_H__ */
