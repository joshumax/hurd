/* Microkernel object module
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */



#include <stdlib.h>
#include <errno.h>

#include <mom-errors.h>

/* This header file defines structure layouts for the use of functions
   below; it is specific to the particular microkernel in use. */
#include <mom-kerndep.h>





/* User RPC endpoints */

/* A communications end-point suitable for sending RPC's to servers. */
struct mom_port_ref;		/* layout defined in mom-kerndep.h */

/* Add a reference to to port reference OBJ. */
error_t mom_add_ref (struct mom_port_ref *obj);

/* Drop a reference from port reference OBJ.  If this is the last reference,
   then OBJ should no longer be used for further mom operations. */
error_t mom_drop_ref (struct mom_port_ref *obj);

/* Create a new port reference that refers to the same underlying channel
   as OBJ.  Fill *NEW with the new reference.  NEW should be otherwise
   unused memory.  The new reference will have a refcount of one (as if
   mom_add_ref had been called on it already).  */
error_t mom_copy_ref (struct mom_port_ref *new, struct mom_port_ref *obj);

/* Tell if two mom ports refer to the same underlying server RPC channel */
int mom_refs_identical (struct mom_port_ref *obj1, struct mom_port_ref *obj2);

/* Return a hash key for a port.  Different ports may have the same
   hash key, but no port's hash key will ever change as long as that
   port is known to this task.  Two identical ports (as by
   mom_ports_identical) will always have the same hash key. */
int mom_hash_ref (struct mom_port_ref *obj);

/* Destroy mom port reference OBJ.  All existing references go away,
   and the underlying kernel object is deallocated.  After this call,
   the memory in *OBJ may be used by the user for any purpose.  It
   is an error to call this routine if any other thread might be calling
   any other mom port reference function on OBJ concurrently.  */
void mom_ref_destroy (struct mom_port_ref *obj);



/* Memory management */

/* Size of a physical page; mom memory management calls must be in
   aligned multiples of this value. */
extern size_t mom_page_size;

/* Reserve a region of memory from START and continuing for LEN bytes
   so that it won't be used by anyone, but don't make it directly
   usable.  */
error_t mom_reserve_memory (void *start, size_t len);

/* Reserve a region of memory anywhere of size LEN bytes and return
   its address in ADDR. */
error_t mom_reserve_memory_anywhere (void **addr, size_t len);

/* Make a reserved region of memory usable, as specified by START and
   LEN.  If READONLY is set then only make it available for read
   access; otherwise permit both read and write.  If OBJ is null, then
   use zero-filled anonymous storage.  If OBJ is non-null, then it
   specifies a mom port reference referring to a memory server, and
   OFFSET is the offset within that server.  If COPY is set, then the
   data is copied from the memory object, otherwise it shares with
   other users of the same object.  */
error_t mom_use_memory (void *start, size_t len, int readonly,
			struct mom_port_ref *obj, size_t offset,
			int copy);

/* Ask the kernel to wire the region of memory specified to physical
   memory.  The exact semantics of this are kernel dependent; it is
   also usually privileged in some fashion and will fail for
   non-privileged users. */
error_t mom_wire_memory (void *start, size_t len);

/* Convert a region of usable memory to read-only */
error_t mom_make_memory_readonly (void *start, size_t len);

/* Convert a region of usable memory to read/write */
error_t mom_make_memory_readwrite (void *start, size_t len);

/* Convert a region of usable memory to reserved but unusable status. */
error_t mom_unuse_memory (void *start, size_t len);

/* Convert a region of reserved unusable memory to unreserved status. */
error_t mom_unreserve_memory (void *start, size_t len);



/* Optimized combination versions of memory functions; these are very
   likely to be faster than using the two call sequences they are
   equivalent to.  */

/* Combined version of mom_unuse_memory followed by mom_unreserve_memory. */
error_t mom_deallocate_memory (void *start, size_t len);

/* Combined version of mom_reserve_memory and mom_use_memory. */
error_t mom_allocate_address (void *start, size_t len, int readonly,
			      struct mom_port_ref *obj, size_t offset,
			      int copy);

/* Combined version of mom_reserve_memory_anywhere and mom_use_memory. */
error_t mom_allocate_memory (void **start, size_t len, int readonly,
			     struct mom_port_ref *obj, size_t offset,
			     int copy);

/* Shorthand for the most common sort of allocation--like mom_allocate_memory,
   but READONLY, and OBJ are both null. */
#define mom_allocate(start,len) \
  (mom_allocate_memory ((start), (len), 0, 0, 0, 0))

