/* Store I/O

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef __STORE_H__
#define __STORE_H__

#include <sys/types.h>

#include <mach.h>
#include <device/device.h>
#include <hurd/hurd_types.h>

struct store
{
  /* If this store was created using store_create, the file from which we got
     our store.  */
  file_t source;

  /* The type of storage this is (see STORAGE_ in hurd/hurd_types.h).  */
  enum file_storage_class class;

  /* Address ranges in the underlying storage which make up our contiguous
     address space.  In units of BLOCK_SIZE, below.  */
  off_t *runs;			/* Malloced */
  size_t runs_len;		/* Length of RUNS.  */
  off_t end;			/* Maximum valid offset.  */
  off_t wrap;			/* If 0, no wrap, otherwise RUNS describes a
				   repeating pattern, of length WRAP -- each
				   successive iteration having an additional
				   offset of WRAP.  */

  /* Handles for the underlying storage.  */
  char *name;			/* Malloced */
  mach_port_t port;		/* Send right */

  /* The size of a `block' on this storage.  */
  size_t block_size;

  /* The number of blocks (of size BLOCK_SIZE) in this storage.  */
  size_t blocks;
  /* The number of bytes in this storage, including holes.  */
  size_t size;

  /* Log_2 (BLOCK_SIZE) or 0 if not a power of 2. */
  int log2_block_size;
  /* Log_2 (VM_PAGE_SIZE / BLOCK_SIZE); only valid if LOG2_BLOCK_SIZE is.  */
  int log2_blocks_per_page;

  void *misc;

  struct store_meths *meths;

  void *hook;			/* Type specific noise.  */
};

typedef error_t (*store_write_meth_t)(struct store *store,
				      off_t addr, size_t index,
				      char *buf, mach_msg_type_number_t len,
				      mach_msg_type_number_t *amount);
typedef error_t (*store_read_meth_t)(struct store *store,
				     off_t addr, size_t index,
				     mach_msg_type_number_t amount,
				     char **buf, mach_msg_type_number_t *len);

struct store_meths
{
  /* Read up to AMOUNT bytes at the underlying address ADDR from the storage
     into BUF and LEN.  INDEX varies from 0 to the number of runs in STORE. */
  store_read_meth_t read;
  /* Write up to LEN bytes from BUF to the storage at the underlying address
     ADDR.  INDEX varies from 0 to the number of runs in STORE. */
  store_write_meth_t write;
  /* Called just before deallocating STORE.  */
  void (*cleanup) (struct store *store);
};

/* Return a new store in STORE, which refers to the storage underlying
   SOURCE.  A reference to SOURCE is created (but may be destroyed with
   store_close_source).  */
error_t store_create (file_t source, struct store **store);

/* Return a new store in STORE referring to the mach device DEVICE.  Consumes
   the send right DEVICE.  */
error_t store_device_create (device_t device, struct store **store);

/* Like store_device_create, but doesn't query the device for information.   */
error_t _store_device_create (device_t device, size_t block_size,
			      off_t *runs, size_t runs_len,
			      struct store **store);

/* Return a new store in STORE referring to the file FILE.  Unlike
   store_create, this will always use file i/o, even it would be possible to
   be more direct.  This may work in more cases, for instance if the file has
   holes.  Consumes the send right FILE.  */
error_t store_file_create (file_t file, struct store **store);

/* Like store_file_create, but doesn't query the file for information.  */
error_t _store_file_create (file_t file, size_t block_size,
			    off_t *runs, size_t runs_len,
			    struct store **store);

/* Return a new store in STORE that interleaves all the stores in STRIPES
   (NUM_STRIPES of them) every INTERLEAVE blocks (every store in STRIPES must
   have the same block size).  If DEALLOC is true, then the striped stores
   are freed when this store is (in any case, the array STRIPES is copied,
   and so should be freed by the caller).  */
error_t store_ileave_create (struct store **stripes, size_t num_stripes,
			     int dealloc,
			     off_t interleave, struct store **store);

/* Return a new store in STORE that concatenates all the stores in STORES
   (NUM_STORES of them) every store in STRIPES must have the same block size.
   If DEALLOC is true, then the sub-stores are freed when this store is (in
   any case, the array STORES is copied, and so should be freed by the
   caller).  */
error_t store_concat_create (struct store **stores, size_t num_stores,
			     int dealloc, struct store **store);

void store_free (struct store *store);

/* Allocate a new store structure of class CLASS, with meths METHS, and the
   various other fields initialized to the given parameters.  */
struct store *
_make_store (enum file_storage_class class, struct store_meths *meths,
	     mach_port_t port, size_t block_size,
	     off_t *runs, size_t runs_len, off_t end);

/* Set STORE's current runs list to (a copy of) RUNS and RUNS_LEN.  */
error_t store_set_runs (struct store *store, off_t *runs, size_t runs_len);

/* Sets the name associated with STORE to a copy of NAME.  */
error_t store_set_name (struct store *store, const char *name);

/* Fills in the values of the various fields in STORE that are derivable from
   the set of runs & the block size.  */
void _store_derive (struct store *store);

/* Write LEN bytes from BUF to STORE at ADDR.  Returns the amount written in
   AMOUNT (in bytes).  ADDR is in BLOCKS (as defined by STORE->block_size).  */
error_t store_write (struct store *store,
		     off_t addr, char *buf, size_t len, size_t *amount);

/* Read AMOUNT bytes from STORE at ADDR into BUF & LEN (which following the
   usual mach buffer-return semantics) to STORE at ADDR.  ADDR is in BLOCKS
   (as defined by STORE->block_size).  Note that LEN is in bytes.  */
error_t store_read (struct store *store,
		    off_t addr, size_t amount, char **buf, size_t *len);

/* If STORE was created using store_create, remove the reference to the
   source from which it was created.  */
void store_close_source (struct store *store);

#if 0

/* Return a memory object paging on STORE.  [among other reasons,] this may
   fail because store contains non-contiguous regions on the underlying
   object.  In such a case you can try calling some of the routines below to
   get a pager.  */
error_t store_map (struct store *store, vm_prot_t prot, ...,
		   mach_port_t *pager);

/* Returns a memory object paging on the file from which STORE was created.
   If STORE wasn't created using store_create, or the source was destroyed
   using store_close_source, this will fail.  */
error_t store_map_source (struct store *store, vm_prot_t prot, ...,
			  mach_port_t *pager)

/* Create a new pager and paging threads paging on STORE, and return the
   resulting memory object in PAGER.  */
error_t store_create_pager (struct store *store, vm_prot_t prot, ...,
			    mach_port_t *pager)

#endif

#endif /* __STORE_H__ */
