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

/* A portion of a store.  If START == -1, it's a hole.  */
struct store_run
{
  off_t start, length;
};

struct store
{
  /* If this store was created using store_create, the file from which we got
     our store.  */
  file_t source;

  /* The type of storage this is (see STORAGE_ in hurd/hurd_types.h).  */
  enum file_storage_class class;

  /* Address ranges in the underlying storage which make up our contiguous
     address space.  In units of BLOCK_SIZE, below.  */
  struct store_run *runs;	/* Malloced */
  size_t num_runs;		/* Length of RUNS.  */

  /* Maximum valid offset.  This is the same as SIZE, but in blocks.  */
  off_t end;

  /* WRAP_SRC is the sum of the run lengths in RUNS.  If this is less than
     END, then RUNS describes a repeating pattern, of length WRAP_SRC -- each
     successive iteration having an additional offset of WRAP_DST.  */
  off_t wrap_src;
  off_t wrap_dst;		/* Only meaningful if WRAP_SRC < END */

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

  /* Random flags.  */
  int flags;

  void *misc;			/* malloced */
  size_t misc_len;

  struct store_meths *meths;

  /* A list of sub-stores.  The interpretation of this is type-specific.  */
  struct store **children;
  size_t num_children;

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

struct store_enc;		/* fwd decl */

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

  /* To the lengths of each for the four arrays in ENC, add how much STORE
     would need to be encoded.  */
  error_t (*allocate_encoding)(const struct store *store,
			       struct store_enc *enc);
  /* Append the encoding for STORE to ENC.  */
  error_t (*encode) (const struct store *store, struct store_enc *enc);

  /* Copy any format-dependent fields in FROM to TO; if there's some reason
     why the copy can't be made, an error should be returned.  This call is
     made after all format-indendependent fields have been cloned.  */
  error_t (*clone) (const struct store *from, struct store *to);
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
			      const struct store_run *runs, size_t num_runs,
			      struct store **store);

/* Return a new store in STORE referring to the file FILE.  Unlike
   store_create, this will always use file i/o, even it would be possible to
   be more direct.  This may work in more cases, for instance if the file has
   holes.  Consumes the send right FILE.  */
error_t store_file_create (file_t file, struct store **store);

/* Like store_file_create, but doesn't query the file for information.  */
error_t _store_file_create (file_t file, size_t block_size,
			    const struct store_run *runs, size_t num_runs,
			    struct store **store);

/* Return a new store in STORE that interleaves all the stores in STRIPES
   (NUM_STRIPES of them) every INTERLEAVE bytes; INTERLEAVE must be an
   integer multiple of each stripe's block size.  The stores in STRIPES are
   consumed -- that is, will be freed when this store is (however, the
   *array* STRIPES is copied, and so should be freed by the caller).  */
error_t store_ileave_create (struct store * const *stripes, size_t num_stripes,
			     off_t interleave, struct store **store);

/* Return a new store in STORE that concatenates all the stores in STORES
   (NUM_STORES of them).  The stores in STRIPES are consumed -- that is, will
   be freed when this store is (however, the *array* STRIPES is copied, and
   so should be freed by the caller).  */
error_t store_concat_create (struct store * const *stores, size_t num_stores,
			     struct store **store);

void store_free (struct store *store);

/* Allocate a new store structure of class CLASS, with meths METHS, and the
   various other fields initialized to the given parameters.  */
struct store *
_make_store (enum file_storage_class class, struct store_meths *meths,
	     mach_port_t port, size_t block_size,
	     const struct store_run *runs, size_t num_runs, off_t end);

/* Set STORE's current runs list to (a copy of) RUNS and NUM_RUNS.  */
error_t store_set_runs (struct store *store,
			const struct store_run *runs, size_t num_runs);

/* Set STORE's current children to (a copy of) CHILDREN and NUM_CHILDREN
   (note that just the vector CHILDREN is copied, not the actual children).  */
error_t store_set_children (struct store *store,
			    struct store *const *children, size_t num_children);

/* Sets the name associated with STORE to a copy of NAME.  */
error_t store_set_name (struct store *store, const char *name);

/* Fills in the values of the various fields in STORE that are derivable from
   the set of runs & the block size.  */
void _store_derive (struct store *store);

/* Return in TO a copy of FROM.  */
error_t store_clone (struct store *from, struct store **to);

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

/* Used to hold the various bits that make up the representation of a store
   for transmission via rpc.  See <hurd/hurd_types.h> for an explanation of
   the encodings for the various storage types.  */
struct store_enc
{
  /* Each of the four vectors used.  All are vm_allocated.  */
  mach_port_t *ports;
  int *ints;
  off_t *offsets;
  char *data;

  /* The sizes of the vectors.  */
  mach_msg_type_number_t num_ports, num_ints, num_offsets, data_len;

  /* Offsets into the above vectors, for an encoding/decoding in progress. */
  size_t cur_port, cur_int, cur_offset, cur_data;

  /* Each of these is an `initial' version of the associated vector.  When
     store_enc_dealloc is called, any vector that is the same as its `init_'
     version won't be deallocated.  */
  mach_port_t *init_ports;
  int *init_ints;
  off_t *init_offsets;
  char *init_data;
};

/* Initialize ENC.  The given vector and sizes will be used for the encoding
   if they are big enough (otherwise new ones will be automatically
   allocated).  */
void store_enc_init (struct store_enc *enc,
		     mach_port_t *ports, mach_msg_type_number_t num_ports,
		     int *ints, mach_msg_type_number_t num_ints,
		     off_t *offsets, mach_msg_type_number_t num_offsets,
		     char *data, mach_msg_type_number_t data_len);

/* Deallocate storage used by the fields in ENC (but nothing is done with ENC
   itself).  */
void store_enc_dealloc (struct store_enc *enc);

/* Encode STORE into ENC, which should have been prepared with
   store_enc_init, or return an error.  The contents of ENC may then be
   return as the value of file_get_storage_info; if for some reason this
   can't be done, store_enc_dealloc may be used to deallocate the mmemory
   used by the unsent vectors.  */
error_t store_encode (const struct store *store, struct store_enc *enc);

/* Decode ENC, either returning a new store in STORE, or an error.  If
   nothing else is to be done with ENC, its contents may then be freed using
   store_enc_dealloc.  */
error_t store_decode (struct store_enc *enc, struct store **store);

/* Default encoding used for most leaf store types.  */
error_t store_default_leaf_allocate_encoding (struct store *store,
					      struct store_enc *enc);
error_t store_default_leaf_encode (struct store *store, struct store_enc *enc);

/* Decodes the standard leaf encoding that's common to various builtin
   formats, and calls CREATE to actually create the store.  */
error_t store_default_leaf_decode (struct store_enc *enc,
				   error_t (*create)(mach_port_t port,
						     size_t block_size,
						     const struct store_run *runs,
						     size_t num_runs,
						     struct store **store),
				   struct store **store);

#endif /* __STORE_H__ */
