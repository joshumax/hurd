/* Store I/O

   Copyright (C) 1995 Free Software Foundation, Inc.

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

struct store
{
  /* If this store was created using store_create, the file from which we got
     our store.  */
  file_t source;

  /* The type of storage this is (see STORAGE_ in hurd/hurd_types.h).  */
  enum file_storage_class class;

  /* Address ranges in the underlying storage which make up our contiguous
     address space.  In units of BLOCK_SIZE, below.  */
  off_t *runs;
  unsigned runs_len;

  /* Handles for the underlying storage.  */
  char *name;
  mach_port_t port;

  /* The size of a `block' on this storage.  */
  size_t block_size;

  /* The number of blocks in this storage.  */
  size_t blocks;
  size_t size;			/* Just BLOCKS * BLOCK_SIZE */

  /* Log_2 (BLOCK_SIZE) or 0 if not a power of 2. */
  int log2_block_size;
  /* Log_2 (VM_PAGE_SIZE / BLOCK_SIZE); only valid if LOG2_BLOCK_SIZE is.  */
  int log2_blocks_per_page;

  void *misc;

  struct store_meths *meths;
};

typedef error_t (*store_write_meth_t)(struct store *store,
				      off_t addr, char *buf, size_t len,
				      size_t *amount);
typedef error_t (*store_read_meth_t)(struct store *store,
				     off_t addr, size_t amount,
				     char **buf, size_t *len);

struct store_meths
{
  /* Read up to AMOUNT bytes at the underlying address ADDR from the storage
     into BUF and LEN.  */
  store_read_meth_t read;
  /* Write up to LEN bytes from BUF to the storage at the underlying address
     ADDR.  */
  store_write_meth_t write;
};

/* Return a new store in STORE, which refers to the storage underlying
   SOURCE.  A reference to SOURCE is created (but may be destroyed with
   store_close_source).  */
error_t store_create (file_t source, struct store **store);

/* Return a new store in STORE referring to the mach device DEVICE.  */
error_t store_device_create (device_t device, struct store **store);

/* Return a new store in STORE referring to the file FILE.  Unlike
   store_create, this will always use file i/o, even it would be possible to
   be more direct.  This may work in more cases, for instance if the file has
   holes.  */
error_t store_file_create (file_t file, struct store **store);

error_t store_destroy (struct store *store);

/* If STORE was created using store_create, remove the reference to the
   source from which it was created.  */
error_t store_close_source (struct store *store);

error_t store_write (struct store *store,
		     off_t addr, char *buf, size_t len, size_t *amount);
error_t store_read (struct store *store,
		    off_t addr, size_t amount, char **buf, size_t *len);

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

#endif /* __STORE_H__ */
