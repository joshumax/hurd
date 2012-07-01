/* Store I/O

   Copyright (C) 1995,96,97,98,99,2001,02,04,05 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

/* A `store' is a fixed-size block of storage, which can be read and perhaps
   written to.  This library implements many different backends which allow
   the abstract store interface to be used with common types of storage --
   devices, files, memory, tasks, etc.  It also allows stores to be combined
   and filtered in various ways.  */

#ifndef __STORE_H__
#define __STORE_H__

#include <sys/types.h>
#include <fcntl.h>

#include <mach.h>
#include <device/device.h>
#include <hurd/hurd_types.h>
#include <features.h>

#ifdef STORE_DEFINE_EI
#define STORE_EI
#else
#define STORE_EI __extern_inline
#endif

/* Type for addresses inside the store.  */
typedef off64_t store_offset_t;

/* A portion of a store.  If START == -1, it's a hole.  */
struct store_run
{
  store_offset_t start, length;
};

struct store
{
  /* If this store was created using store_create, the file from which we got
     our store.  */
  file_t source;

  /* Address ranges in the underlying storage which make up our contiguous
     address space.  In units of BLOCK_SIZE, below.  */
  struct store_run *runs;	/* Malloced */
  size_t num_runs;		/* Length of RUNS.  */

  /* Maximum valid offset.  This is the same as SIZE, but in blocks.  */
  store_offset_t end;

  /* WRAP_SRC is the sum of the run lengths in RUNS.  If this is less than
     END, then RUNS describes a repeating pattern, of length WRAP_SRC -- each
     successive iteration having an additional offset of WRAP_DST.  */
  store_offset_t wrap_src;
  store_offset_t wrap_dst;	/* Only meaningful if WRAP_SRC < END */

  /* Handles for the underlying storage.  */
  char *name;			/* Malloced */
  mach_port_t port;		/* Send right */

  /* The size of a `block' on this storage.  */
  size_t block_size;

  /* The number of blocks (of size BLOCK_SIZE) in this storage.  */
  store_offset_t blocks;
  /* The number of bytes in this storage, including holes.  */
  store_offset_t size;

  /* Log_2 (BLOCK_SIZE) or 0 if not a power of 2. */
  unsigned log2_block_size;
  /* Log_2 (VM_PAGE_SIZE / BLOCK_SIZE); only valid if LOG2_BLOCK_SIZE is.  */
  unsigned log2_blocks_per_page;

  /* Random flags.  */
  int flags;

  void *misc;			/* malloced */
  size_t misc_len;

  const struct store_class *class;

  /* A list of sub-stores.  The interpretation of this is type-specific.  */
  struct store **children;
  size_t num_children;

  void *hook;			/* Type specific noise.  */
};

/* Store flags.  These are in addition to the STORAGE_ flags defined in
   <hurd/hurd_types.h>.  XXX synchronize these values.  */

/* Flags that reflect something immutable about the object.  */
#define STORE_IMMUTABLE_FLAGS	0x00FF

/* Flags implemented by generic store code.  */
#define STORE_READONLY		0x0100	/* No writing allowed. */
#define STORE_NO_FILEIO		0x0200	/* If store_create can't fetch store
					   information, don't create a store
					   using file io instead.  */
#define STORE_GENERIC_FLAGS	(STORE_READONLY | STORE_NO_FILEIO)

/* Flags implemented by each backend.  */
#define STORE_HARD_READONLY	0x1000 /* Can't be made writable.  */
#define STORE_ENFORCED		0x2000 /* Range is enforced by device.  */
#define STORE_INACTIVE		0x4000 /* Not in a usable state.  */
#define STORE_INNOCUOUS		0x8000 /* Cannot modify anything dangerous. */
#define STORE_BACKEND_SPEC_BASE	0x10000 /* Here up are backend-specific */
#define STORE_BACKEND_FLAGS	(STORE_HARD_READONLY | STORE_ENFORCED \
				 | STORE_INACTIVE \
				 | ~(STORE_BACKEND_SPEC_BASE - 1))

typedef error_t (*store_write_meth_t)(struct store *store,
				      store_offset_t addr, size_t index,
				      const void *buf,
				      mach_msg_type_number_t len,
				      mach_msg_type_number_t *amount);
typedef error_t (*store_read_meth_t)(struct store *store,
				     store_offset_t addr, size_t index,
				     mach_msg_type_number_t amount,
				     void **buf, mach_msg_type_number_t *len);
typedef error_t (*store_set_size_meth_t)(struct store *store,
					 size_t newsize);

struct store_enc;		/* fwd decl */

struct store_class
{
  /* The type of storage this is (see STORAGE_ in hurd/hurd_types.h).  */
  enum file_storage_class id;

  /* Name of the class.  */
  const char *name;

  /* Read up to AMOUNT bytes at the underlying address ADDR from the storage
     into BUF and LEN.  INDEX varies from 0 to the number of runs in STORE.  */
  store_read_meth_t read;
  /* Write up to LEN bytes from BUF to the storage at the underlying address
     ADDR.  INDEX varies from 0 to the number of runs in STORE.  */
  store_write_meth_t write;
  /* Set store's size to NEWSIZE (in bytes).  */
  store_set_size_meth_t set_size;

  /* To the lengths of each for the four arrays in ENC, add how much STORE
     would need to be encoded.  */
  error_t (*allocate_encoding)(const struct store *store,
			       struct store_enc *enc);
  /* Append the encoding for STORE to ENC.  */
  error_t (*encode) (const struct store *store, struct store_enc *enc);

  /* Decode from ENC a new store, which return in STORE.  CLASSES is used to
     lookup child classes.  */
  error_t (*decode) (struct store_enc *enc,
		     const struct store_class *const *classes,
		     struct store **store);

  /* Modify flags that reflect backend state, such as STORE_HARD_READONLY and
     STORE_ENFORCED.  */
  error_t (*set_flags) (struct store *store, int flags);
  error_t (*clear_flags) (struct store *store, int flags);

  /* Called just before deallocating STORE.  */
  void (*cleanup) (struct store *store);

  /* Copy any format-dependent fields in FROM to TO; if there's some reason
     why the copy can't be made, an error should be returned.  This call is
     made after all format-indendependent fields have been cloned.  */
  error_t (*clone) (const struct store *from, struct store *to);

  /* Return in STORE a store that only contains the parts of SOURCE as
     enumerated in RUNS & RUNS_LEN, consuming SOURCE in the process.  The
     default behavior, if REMAP is 0, is to replace SOURCE's run list with
     the subset selected by RUNS, and return SOURCE.  */
  error_t (*remap) (struct store *source,
		    const struct store_run *runs, size_t num_runs,
		    struct store **store);

  /* Open a new store called NAME in this class.  CLASSES is supplied in case
     it's desirable to open a sub-store in some manner.  */
  error_t (*open) (const char *name, int flags,
		   const struct store_class *const *classes,
		   struct store **store);

  /* Given a user argument ARG, this function should check it for syntactic
     validity, or print a syntax error, using ARGP_STATE in the normal
     manner; if zero is returned, then this argument is assumed valid, and
     can be passed to the open function.  If ARG is 0, then there were *no*
     arguments specified; in this case, returning EINVAL means that this is
     not kosher.  If PARSE is 0, then it is assumed that if this class has an
     OPEN function, then validity can't be syntactically determined.  */
  error_t (*validate_name) (const char *name,
			    const struct store_class *const *classes);

  /* Return a memory object paging on STORE.  */
  error_t (*map) (const struct store *store, vm_prot_t prot, mach_port_t *memobj);
};

/* Return a new store in STORE, which refers to the storage underlying
   SOURCE.  CLASSES is as if passed to store_find_class, which see.  FLAGS
   is set with store_set_flags, with the exception of STORE_INACTIVE, which
   merely indicates that no attempt should be made to activate an inactive
   store; if STORE_INACTIVE is not specified, and the store returned for
   SOURCE is inactive, an attempt is made to activate it (failure of which
   causes an error to be returned).  A reference to SOURCE is created (but
   may be destroyed with store_close_source).  */
error_t store_create (file_t source, int flags,
		      const struct store_class *const *classes,
		      struct store **store);

void store_free (struct store *store);

/* Open the file NAME, and return a new store in STORE, which refers to the
   storage underlying it.  CLASSES is as if passed to store_find_class,
   which see.  FLAGS is set with store_set_flags.  A reference to the open
   file is created (but may be destroyed with store_close_source).  */
error_t store_open (const char *name, int flags,
		    const struct store_class *const *classes,
		    struct store **store);

/* Allocate a new store structure, returned in STORE, with class CLASS and
   the various other fields initialized to the given parameters.  */
error_t
_store_create (const struct store_class *class, mach_port_t port,
	       int flags, size_t block_size,
	       const struct store_run *runs, size_t num_runs,
	       store_offset_t end, struct store **store);

/* Set STORE's current runs list to (a copy of) RUNS and NUM_RUNS.  */
error_t store_set_runs (struct store *store,
			const struct store_run *runs, size_t num_runs);

/* Set STORE's current children to (a copy of) CHILDREN and NUM_CHILDREN
   (note that just the vector CHILDREN is copied, not the actual children).  */
error_t store_set_children (struct store *store,
			    struct store *const *children, size_t num_children);

/* Try to come up with a name for the children in STORE, combining the names
   of each child in a way that could be used to parse them with
   store_open_children.  This is done heuristically, and so may not succeed.
   If a child doesn't have a  name, EINVAL is returned.  */
error_t store_children_name (const struct store *store, char **name);

/* Sets the name associated with STORE to a copy of NAME.  */
error_t store_set_name (struct store *store, const char *name);

/* Add FLAGS to STORE's currently set flags.  */
error_t store_set_flags (struct store *store, int flags);

/* Remove FLAGS from STORE's currently set flags.  */
error_t store_clear_flags (struct store *store, int flags);

/* Set FLAGS in all children of STORE, and if successful, add FLAGS to
   STORE's flags.  */
error_t store_set_child_flags (struct store *store, int flags);

/* Clear FLAGS in all children of STORE, and if successful, remove FLAGS from
   STORE's flags.  */
error_t store_clear_child_flags (struct store *store, int flags);

extern int store_is_securely_returnable (struct store *store, int open_flags);

#if defined(__USE_EXTERN_INLINES) || defined(STORE_DEFINE_EI)

/* Returns true if STORE can safely be returned to a user who has accessed it
   via a node using OPEN_FLAGS, without compromising security.  */
STORE_EI int
store_is_securely_returnable (struct store *store, int open_flags)
{
  int flags = store->flags;
  return
    (flags & (STORE_INNOCUOUS | STORE_INACTIVE))
    || ((flags & STORE_ENFORCED)
	&& (((open_flags & O_ACCMODE) == O_RDWR)
	    || (flags & STORE_HARD_READONLY)));
}

#endif /* Use extern inlines.  */

/* Fills in the values of the various fields in STORE that are derivable from
   the set of runs & the block size.  */
void _store_derive (struct store *store);

/* Return in TO a copy of FROM.  */
error_t store_clone (struct store *from, struct store **to);

/* Return a store in STORE that reflects the blocks in RUNS & RUNS_LEN from
   source; SOURCE is consumed, but not RUNS.  Unlike the store_remap_create
   function, this may simply modify SOURCE and return it.  */
error_t store_remap (struct store *source,
		     const struct store_run *runs, size_t num_runs,
		     struct store **store);

/* Write LEN bytes from BUF to STORE at ADDR.  Returns the amount written in
   AMOUNT (in bytes).  ADDR is in BLOCKS (as defined by STORE->block_size).  */
error_t store_write (struct store *store,
		     store_offset_t addr, const void *buf, size_t len,
		     size_t *amount);

/* Read AMOUNT bytes from STORE at ADDR into BUF & LEN (which following the
   usual mach buffer-return semantics) to STORE at ADDR.  ADDR is in BLOCKS
   (as defined by STORE->block_size).  Note that LEN is in bytes.  */
error_t store_read (struct store *store,
		    store_offset_t addr, size_t amount, void **buf, size_t *len);

/* Set STORE's size to NEWSIZE (in bytes).  */
error_t store_set_size (struct store *store, size_t newsize);

/* If STORE was created using store_create, remove the reference to the
   source from which it was created.  */
void store_close_source (struct store *store);

/* Return a memory object paging on STORE.  If this call fails with
   EOPNOTSUPP, you can try calling some of the routines below to get a pager.  */
error_t store_map (const struct store *store, vm_prot_t prot,
		   mach_port_t *memobj);

#if 0

/* Create a new pager and paging threads paging on STORE, and return the
   resulting memory object in PAGER.  */
error_t store_create_pager (struct store *store, vm_prot_t prot, ...,
			    mach_port_t *memobj)

#endif

/* Creating specific types of stores.  */

/* Return a new zero store SIZE bytes long in STORE.  */
error_t store_zero_create (store_offset_t size, int flags, struct store **store);

/* Return a new store in STORE referring to the mach device DEVICE.  Consumes
   the send right DEVICE.  */
error_t store_device_create (device_t device, int flags, struct store **store);

/* Like store_device_create, but doesn't query the device for information.   */
error_t _store_device_create (device_t device, int flags, size_t block_size,
			      const struct store_run *runs, size_t num_runs,
			      struct store **store);

/* Open the device NAME, and return the corresponding store in STORE.  */
error_t store_device_open (const char *name, int flags, struct store **store);

/* Return a new store in STORE which contains a remap store of partition
   PART from the contents of SOURCE; SOURCE is consumed.  */
error_t store_part_create (struct store *source, int index, int flags,
                          struct store **store);

/* Open the part NAME.  NAME consists of a partition number, a ':', a
   another store class name, a ':' and a name for to by passed to the
   store class.  E.g. "2:device:hd0" would open the second partition
   on a DEVICE store named "hd0".  FLAGS indicate how to open the
   store.  CLASSES is as if passed to store_find_class, which see.
   The new store is returned in *STORE.  */
error_t store_part_open (const char *name, int flags,
                        const struct store_class *const *classes,
                        struct store **store);

/* Return a new store in STORE referring to the file FILE.  Unlike
   store_create, this will always use file i/o, even it would be possible to
   be more direct.  This may work in more cases, for instance if the file has
   holes.  Consumes the send right FILE.  */
error_t store_file_create (file_t file, int flags, struct store **store);

/* Like store_file_create, but doesn't query the file for information.  */
error_t _store_file_create (file_t file, int flags, size_t block_size,
			    const struct store_run *runs, size_t num_runs,
			    struct store **store);

/* Open the file NAME, and return the corresponding store in STORE.  */
error_t store_file_open (const char *name, int flags, struct store **store);

/* Return a new store in STORE referring to the task TASK, consuming TASK.  */
error_t store_task_create (task_t task, int flags, struct store **store);

/* Like store_task_create, but doesn't query the task for information.  */
error_t _store_task_create (task_t task, int flags, size_t block_size,
			    const struct store_run *runs, size_t num_runs,
			    struct store **store);

/* Open the task NAME (NAME should be the task's pid), and return the
   corresponding store in STORE.  */
error_t store_task_open (const char *name, int flags, struct store **store);

/* Return a new store in STORE referring to the memory object MEMOBJ.
   Consumes the send right MEMOBJ.  */
error_t store_memobj_create (memory_object_t memobj, int flags,
			     size_t block_size,
			     const struct store_run *runs, size_t num_runs,
			     struct store **store);

/* Open the network block device NAME (parsed as "HOSTNAME:PORT[/BLOCKSIZE]"),
   and return the corresponding store in STORE.  This opens a socket and
   initial connection handshake, which determine the size of the device,
   and then uses _store_nbd_create with the open socket port.  */
error_t store_nbd_open (const char *name, int flags, struct store **store);

/* Create a store that works by talking to an nbd server on an existing
   socket port.  */
error_t _store_nbd_create (mach_port_t port, int flags, size_t block_size,
			   const struct store_run *runs, size_t num_runs,
			   struct store **store);

/* Return a new store of type "unknown" that holds a copy of the
   given encoding.  The name of the store is taken from ENC->data.
   Future calls to store_encode/store_return will produce exactly
   the encoding supplied here.  All i/o operations fail with EFTYPE.  */
error_t store_unknown_decode (struct store_enc *enc,
			      const struct store_class *const *classes,
			      struct store **store);

/* Return a new store in STORE that interleaves all the stores in STRIPES
   (NUM_STRIPES of them) every INTERLEAVE bytes; INTERLEAVE must be an
   integer multiple of each stripe's block size.  The stores in STRIPES are
   consumed -- that is, will be freed when this store is (however, the
   *array* STRIPES is copied, and so should be freed by the caller).  */
error_t store_ileave_create (struct store * const *stripes, size_t num_stripes,
			     store_offset_t interleave, int flags,
			     struct store **store);

/* Return a new store in STORE that concatenates all the stores in STORES
   (NUM_STORES of them).  The stores in STRIPES are consumed -- that is, will
   be freed when this store is (however, the *array* STRIPES is copied, and
   so should be freed by the caller).  */
error_t store_concat_create (struct store * const *stores, size_t num_stores,
			     int flags, struct store **store);

/* Return a new store that concatenates the stores created by opening all the
   individual stores described in NAME; for the syntax of NAME, see
   store_open_children.  */
error_t store_concat_open (const char *name, int flags,
			   const struct store_class *const *classes,
			   struct store **store);

/* Return a new store in STORE that reflects the blocks in RUNS & RUNS_LEN
   from SOURCE; SOURCE is consumed, but RUNS is not.  Unlike the store_remap
   function, this function always operates by creating a new store of type
   `remap' which has SOURCE as a child, and so may be less efficient than
   store_remap for some types of stores.  */
error_t store_remap_create (struct store *source,
			    const struct store_run *runs, size_t num_runs,
			    int flags, struct store **store);

/* Return a new store in STORE which contains a snapshot of the contents of
   the store FROM; FROM is consumed.  */
error_t store_copy_create (struct store *from, int flags, struct store **store);

/* Open the copy store NAME -- which consists of another store-class
   name, a ':', and a name for that store class to open -- and return
   the corresponding store in STORE.  CLASSES is as if passed to
   store_find_class, which see.  */
error_t store_copy_open (const char *name, int flags,
			 const struct store_class *const *classes,
			 struct store **store);

/* Return a new store in STORE which contains the memory buffer BUF, of
   length BUF_LEN.  BUF must be vm_allocated, and will be consumed.  */
error_t store_buffer_create (void *buf, size_t buf_len, int flags,
			     struct store **store);

/* Return a new store in STORE which contains a snapshot of the uncompressed
   contents of the store FROM; FROM is consumed.  BLOCK_SIZE is the desired
   block size of the result.  */
error_t store_gunzip_create (struct store *from, int flags,
			     struct store **store);

/* Open the gunzip NAME -- which consists of another store-class name, a
   ':', and a name for that store class to open -- and return the
   corresponding store in STORE.  CLASSES is as if passed to
   store_find_class, which see.  */
error_t store_gunzip_open (const char *name, int flags,
			   const struct store_class *const *classes,
			   struct store **store);

/* Return a new store in STORE which contains a snapshot of the uncompressed
   contents of the store FROM; FROM is consumed.  BLOCK_SIZE is the desired
   block size of the result.  */
error_t store_bunzip2_create (struct store *from, int flags,
			      struct store **store);

/* Open the bunzip2 NAME -- which consists of another store-class name, a ':',
   and a name for that store class to open -- and return the corresponding
   store in STORE.  CLASSES is as if passed to store_find_class, which see.  */
error_t store_bunzip2_open (const char *name, int flags,
			    const struct store_class *const *classes,
			    struct store **store);

/* Return a new store in STORE that multiplexes multiple physical volumes
   from PHYS as one larger virtual volume.  SWAP_VOLS is a function that will
   be called whenever the volume currently active isn't correct.  PHYS is
   consumed.  */
error_t store_mvol_create (struct store *phys,
			   error_t (*swap_vols) (struct store *store, size_t new_vol,
						 ssize_t old_vol),
			   int flags,
			   struct store **store);

/* Opening stores from a standard set of store classes.

   These first two functions underlie the following functions, and
   other functions such as store_open taking a CLASSES argument that
   can be null.  The standard set of classes to be searched when that
   argument is null includes all the `const struct store_class *'
   pointers found in the `store_std_classes' section of the executable
   and all loaded shared objects; store_find_class searches that set
   for the named class.  The store_typed_open and store_url_open
   functions also try store_module_find_class, but only if the
   function has already been linked in; it's always available in the
   shared library, and available for static linking with
   -lstore_module -ldl.

   The macro STORE_STD_CLASS produces a reference in the `store_std_classes'
   section, so that linking in a module containing that definition will add
   the referenced class to the standard search list.  In the shared library,
   the various standard classes are included this way.  In the static
   library, only the pseudo classes like `query' and `typed' will normally
   be linked in (via referenced to store_open and so forth); to force
   specific store type modules to be linked in, you must specify an
   `-lstore_CLASS' option for each individual class to be statically linked.
*/

/* Find a store class by name.  CLNAME_END points to the character
   after the class name NAME points to; if null, then NAME is just the
   null-terminated class name.  */
const struct store_class *
store_find_class (const char *name,
		  const char *clname_end,
		  const struct store_class *const *classes);

/* This is the underlying function that tries to load a module to
   define the store type called NAME.  On success, returns zero
   and sets *CLASSP to the descriptor found.  Returns ENOENT if
   there is no such module, or other error codes if there is a
   module but it does not load correctly.  */
error_t store_module_find_class (const char *name,
				 const char *clname_end,
				 const struct store_class **classp);


/* Open the store indicated by NAME, which should consist of a store
   type name followed by a ':' and any type-specific name, returning the
   new store in STORE.  CLASSES is as if passed to store_find_class,
   which see.  */
error_t store_typed_open (const char *name, int flags,
			  const struct store_class *const *classes,
			  struct store **store);

/* Similar to store_typed_open, but NAME must be in URL format, i.e. a
   class name followed by a ':' and any type-specific name.  A leading ':'
   or no ':' at all is invalid syntax.  (See store_module_open, below.)  */
error_t store_url_open (const char *name, int flags,
			const struct store_class *const *classes,
			struct store **store);

/* This attempts to decode a standard-form STORAGE_NETWORK encoding whose
   encoded name is in URL format, by finding the store type indicated in
   the URL (as for store_url_open) and that type's decode function.  */
error_t store_url_decode (struct store_enc *enc,
			  const struct store_class *const *classes,
			  struct store **store);


/* Similar to store_typed_open, but the store type's code is found
   dynamically rather than statically in CLASSES.  A shared object name
   for `dlopen' and symbol names for `dlsym' are derived from the type
   name and used to find the `struct store_class' for the named type.
   (CLASSES is used only by the type's own open function, e.g. if that
   type accepts a child-store syntax in its name.)

   In fact, when this code is linked in (always in the shared library,
   only with `-lstore_module -ldl -lstore' for static linking), all
   the functions documented as using STORE_STD_CLASSES will also
   check for loadable modules if the type name is not found statically.  */
error_t store_module_open (const char *name, int flags,
			   const struct store_class *const *classes,
			   struct store **store);


/* This attempts to find a module that can decode ENC.  If no such
   module can be found it returns ENOENT.  Otherwise it returns
   the result of the loaded store type's `decode' function.  */
error_t store_module_decode (struct store_enc *enc,
			     const struct store_class *const *classes,
			     struct store **store);

/* Parse multiple store names in NAME, and open each individually, returning
   all in the vector STORES, and the number in NUM_STORES.  The syntax of
   NAME is a single non-alpha-numeric separator character, followed by each
   child store name separated by the same separator; each child name is
   TYPE:NAME notation as parsed by store_typed_open.  If every child uses the
   same TYPE: prefix, then it may be factored out and put before the child
   list instead (the two types of notation are differentiated by whether the
   first character of name is alpha-numeric or not).  */
error_t store_open_children (const char *name, int flags,
			     const struct store_class *const *classes,
			     struct store ***stores, size_t *num_stores);


/* Standard store classes implemented by libstore.  */
extern const struct store_class store_device_class;
extern const struct store_class store_part_class;
extern const struct store_class store_file_class;
extern const struct store_class store_task_class;
extern const struct store_class store_nbd_class;
extern const struct store_class store_memobj_class;
extern const struct store_class store_zero_class;
extern const struct store_class store_ileave_class;
extern const struct store_class store_concat_class;
extern const struct store_class store_remap_class;
extern const struct store_class store_query_class;
extern const struct store_class store_copy_class;
extern const struct store_class store_gunzip_class;
extern const struct store_class store_bunzip2_class;
extern const struct store_class store_typed_open_class;
extern const struct store_class store_url_open_class;
extern const struct store_class store_module_open_class;
extern const struct store_class store_unknown_class;

/* The following are not included in STORE_STD_CLASSES.  */
extern const struct store_class store_mvol_class;

#define STORE_STD_CLASS(name) \
  static const struct store_class *const store_std_classes_##name[] \
    __attribute_used__ __attribute__ ((section ("store_std_classes"))) \
    = { &store_##name##_class }


extern const struct store_class *const __start_store_std_classes[] __attribute__ ((weak));
extern const struct store_class *const __stop_store_std_classes[] __attribute__ ((weak));

/* Used to hold the various bits that make up the representation of a store
   for transmission via rpc.  See <hurd/hurd_types.h> for an explanation of
   the encodings for the various storage types.  */
struct store_enc
{
  /* Each of the four vectors used.  All are vm_allocated.  */
  mach_port_t *ports;
  int *ints;
  loff_t *offsets;
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
  loff_t *init_offsets;
  char *init_data;
};

/* Initialize ENC.  The given vector and sizes will be used for the encoding
   if they are big enough (otherwise new ones will be automatically
   allocated).  */
void store_enc_init (struct store_enc *enc,
		     mach_port_t *ports, mach_msg_type_number_t num_ports,
		     int *ints, mach_msg_type_number_t num_ints,
		     loff_t *offsets, mach_msg_type_number_t num_offsets,
		     char *data, mach_msg_type_number_t data_len);

/* Deallocate storage used by the fields in ENC (but nothing is done with ENC
   itself).  */
void store_enc_dealloc (struct store_enc *enc);

/* Copy out the parameters from ENC into the given variables suitably for
   returning from a file_get_storage_info rpc, and deallocate ENC.  */
void store_enc_return (struct store_enc *enc,
		       mach_port_t **ports, mach_msg_type_number_t *num_ports,
		       int **ints, mach_msg_type_number_t *num_ints,
		       loff_t **offsets, mach_msg_type_number_t *num_offsets,
		       char **data, mach_msg_type_number_t *data_len);

/* Encode STORE into the given return variables, suitably for returning from a
   file_get_storage_info rpc.  */
error_t store_return (const struct store *store,
		      mach_port_t **ports, mach_msg_type_number_t *num_ports,
		      int **ints, mach_msg_type_number_t *num_ints,
		      loff_t **offsets, mach_msg_type_number_t *num_offsets,
		      char **data, mach_msg_type_number_t *data_len);

/* Encode STORE into ENC, which should have been prepared with
   store_enc_init, or return an error.  The contents of ENC may then be
   return as the value of file_get_storage_info; if for some reason this
   can't be done, store_enc_dealloc may be used to deallocate the mmemory
   used by the unsent vectors.  */
error_t store_encode (const struct store *store, struct store_enc *enc);

/* Decode ENC, either returning a new store in STORE, or an error.
   CLASSES is as if passed to store_find_class, which see.  If nothing
   else is to be done with ENC, its contents may then be freed using
   store_enc_dealloc.  */
error_t store_decode (struct store_enc *enc,
		      const struct store_class *const *classes,
		      struct store **store);

/* Calls the allocate_encoding method in each child store of STORE,
   propagating any errors.  If any child does not hae such a method,
   EOPNOTSUPP is returned.  */
error_t store_allocate_child_encodings (const struct store *store,
					struct store_enc *enc);

/* Calls the encode method in each child store of STORE, propagating any
   errors.  If any child does not hae such a method, EOPNOTSUPP is returned. */
error_t store_encode_children (const struct store *store,
			       struct store_enc *enc);

/* Decodes NUM_CHILDREN from ENC, storing the results into successive
   positions in CHILDREN.  */
error_t store_decode_children (struct store_enc *enc, int num_children,
			       const struct store_class *const *classes,
			       struct store **children);

/* Call FUN with the vector RUNS of length NUM_RUNS extracted from ENC.  */
error_t store_with_decoded_runs (struct store_enc *enc, size_t num_runs,
				 error_t (*fun) (const struct store_run *runs,
						 size_t num_runs));

/* Standard encoding used for most leaf store types.  */
error_t store_std_leaf_allocate_encoding (const struct store *store,
					  struct store_enc *enc);
error_t store_std_leaf_encode (const struct store *store,
			       struct store_enc *enc);

/* Creation function signature used by store_std_leaf_decode.  */
typedef error_t (*store_std_leaf_create_t)(mach_port_t port,
					   int flags,
					   size_t block_size,
					   const struct store_run *runs,
					   size_t num_runs,
					   struct store **store);

/* Decodes the standard leaf encoding that's common to various builtin
   formats, and calls CREATE to actually create the store.  */
error_t store_std_leaf_decode (struct store_enc *enc,
			       store_std_leaf_create_t create,
			       struct store **store);

/* An argument parser that may be used for parsing a simple command line
   specification for stores.  The accompanying input parameter must be a
   pointer to a struct store_argp_params.  */
extern struct argp store_argp;

/* The structure used to pass args back and forth from STORE_ARGP.  */
struct store_argp_params
{
  /* The resulting parsed result.  */
  struct store_parsed *result;

  /* If --store-type isn't specified use this; 0 is equivalent to "query".  */
  const char *default_type;

  /* The set of classes used to validate store-types and argument syntax. */
  const struct store_class *const *classes;

  /* This controls the behavior when no store arguments are specified.
     If zero, the parser fails with the error message "No store specified".
     If nonzero, the parser succeeds and sets `result' to null.  */
  int store_optional;
};

/* The result of parsing a store, which should be enough information to open
   it, or return the arguments.  */
struct store_parsed;

/* Free all resources used by PARSED.  */
void store_parsed_free (struct store_parsed *parsed);

/* Open PARSED, and return the corresponding store in STORE.  */
error_t store_parsed_open (const struct store_parsed *parsed, int flags,
			   struct store **store);

/* Add the arguments used to create PARSED to ARGZ & ARGZ_LEN.  */
error_t store_parsed_append_args (const struct store_parsed *parsed,
				  char **argz, size_t *argz_len);

/* Make a string describing PARSED, and return it in malloced storage in
   NAME.  */
error_t store_parsed_name (const struct store_parsed *parsed, char **name);


#endif /* __STORE_H__ */
