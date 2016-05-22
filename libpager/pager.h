/* Definitions for multi-threaded pager library
   Copyright (C) 1994, 1995, 1996, 1997, 1999 Free Software Foundation, Inc.

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


#ifndef _HURD_PAGER_
#define _HURD_PAGER_

#include <hurd/ports.h>

/* This declaration exists to place struct user_pager_info in the proper
   scope.  */
struct user_pager_info;

struct pager_requests;

/* Start the worker threads libpager uses to service requests. If no
   error is returned, *requests will be a valid pointer, else it will be
   set to NULL.  */
error_t
pager_start_workers (struct port_bucket *pager_bucket,
		     struct pager_requests **requests);

/* Inhibit the worker threads libpager uses to service requests,
   blocking until all requests sent before this function is called have
   finished.
   Note that RPCs will not be inhibited, so new requests will
   queue up, but will not be handled until the workers are resumed. If
   RPCs should be inhibited as well, call ports_inhibit_bucket_rpcs with
   the bucket used to create the workers before calling this. However,
   inhibiting RPCs and not calling this is generally insufficient, as
   libports is unaware of our internal worker pool, and will return once
   all the RPCs have been queued, before they have been handled by a
   worker thread.  */
error_t
pager_inhibit_workers (struct pager_requests *requests);

/* Resume the worker threads libpager uses to service requests.  */
void
pager_resume_workers (struct pager_requests *requests);

/* Create a new pager.  The pager will have a port created for it
   (using libports, in BUCKET) and will be immediately ready
   to receive requests.  U_PAGER will be provided to later calls to
   pager_find_address.  The pager will have one user reference
   created.  MAY_CACHE and COPY_STRATEGY are the original values of
   those attributes as for memory_object_ready.  If NOTIFY_ON_EVICT is
   non-zero, pager_notify_evict user callback will be called when page
   is evicted.  Users may create references to pagers by use of the
   relevant ports library functions.  On errors, return null and set
   errno.  */
struct pager *
pager_create (struct user_pager_info *u_pager,
	      struct port_bucket *bucket,
	      boolean_t may_cache,
	      memory_object_copy_strategy_t copy_strategy,
	      boolean_t notify_on_evict);

/* Likewise, but also allocate space for the user hook.  */
struct pager *
pager_create_alloc (size_t u_pager_size,
		    struct port_bucket *bucket,
		    boolean_t may_cache,
		    memory_object_copy_strategy_t copy_strategy,
		    boolean_t notify_on_evict);

/* Return the user_pager_info struct associated with a pager. */
struct user_pager_info *
pager_get_upi (struct pager *p);

/* Sync data from pager PAGER to backing store; wait for
   all the writes to complete iff WAIT is set. */
void
pager_sync (struct pager *pager,
	    int wait);

/* Sync some data (starting at START, for LEN bytes) from pager PAGER
   to backing store.  Wait for all the writes to complete iff WAIT is
   set.  */
void
pager_sync_some (struct pager *pager,
		 vm_address_t start,
		 vm_size_t len,
		 int wait);

/* Flush data from the kernel for pager PAGER and force any pending
   delayed copies.  Wait for all pages to be flushed iff WAIT is set. */
void
pager_flush (struct pager *pager,
	     int wait);


/* Flush some data (starting at START, for LEN bytes) for pager PAGER
   from the kernel.  Wait for all pages to be flushed iff WAIT is set.  */
void
pager_flush_some (struct pager *pager,
		  vm_address_t start,
		  vm_size_t len,
		  int wait);

/* Flush data from the kernel for pager PAGER and force any pending
   delayed copies.  Wait for all pages to be flushed iff WAIT is set.
   Have the kernel write back modifications.  */
void
pager_return (struct pager *pager,
	      int wait);


/* Flush some data (starting at START, for LEN bytes) for pager PAGER
   from the kernel.  Wait for all pages to be flushed iff WAIT is set.  
   Have the kernel write back modifications. */
void
pager_return_some (struct pager *pager,
		   vm_address_t start,
		   vm_size_t len,
		   int wait);

/* Offer a page of data to the kernel.  If PRECIOUS is set, then this
   page will be paged out at some future point, otherwise it might be
   dropped by the kernel.  If the page is currently in core, the
   kernel might ignore this call.  */
void
pager_offer_page (struct pager *pager,
		  int precious,
		  int writelock,
		  vm_offset_t page,
		  vm_address_t buf);  

/* Change the attributes of the memory object underlying pager PAGER.
   Arguments MAY_CACHE and COPY_STRATEGY are as for
   memory_object_change_attributes.  Wait for the kernel to report
   completion if WAIT is set.  */
void
pager_change_attributes (struct pager *pager,
			 boolean_t may_cache,
			 memory_object_copy_strategy_t copy_strategy,
			 int wait);

/* Return the port (receive right) for requests to the pager.  It is
   absolutely necessary that a new send right be created from this
   receive right.  */
mach_port_t
pager_get_port (struct pager *pager);

/* Force termination of a pager.  After this returns, no
   more paging requests on the pager will be honored, and the
   pager will be deallocated.  (The actual deallocation might
   occur asynchronously if there are currently outstanding paging
   requests that will complete first.)  */
void
pager_shutdown (struct pager *pager);

/* Return the error code of the last page error for pager P at address ADDR;
   this will be deleted when the kernel interface is fixed.  */
error_t
pager_get_error (struct pager *p, vm_address_t addr);

/* Try to copy *SIZE bytes between the region OTHER points to
   and the region at OFFSET in the pager indicated by PAGER and MEMOBJ.
   If PROT is VM_PROT_READ, copying is from the pager to OTHER;
   if PROT contains VM_PROT_WRITE, copying is from OTHER into the pager.
   *SIZE is always filled in the actual number of bytes successfully copied.
   Returns an error code if the pager-backed memory faults;
   if there is no fault, returns 0 and *SIZE will be unchanged.  */
error_t
pager_memcpy (struct pager *pager, memory_object_t memobj,
	      vm_offset_t offset, void *other, size_t *size,
	      vm_prot_t prot);

/* The user must define this function.  For pager PAGER, read one
   page from offset PAGE.  Set *BUF to be the address of the page,
   and set *WRITE_LOCK if the page must be provided read-only.
   The only permissible error returns are EIO, EDQUOT, and ENOSPC. */
error_t
pager_read_page (struct user_pager_info *pager,
		 vm_offset_t page,
		 vm_address_t *buf,
		 int *write_lock);

/* The user must define this function.  For pager PAGER, synchronously
   write one page from BUF to offset PAGE.  In addition, mfree
   (or equivalent) BUF.  The only permissible error returns are EIO,
   EDQUOT, and ENOSPC. */
error_t
pager_write_page (struct user_pager_info *pager,
		  vm_offset_t page,
		  vm_address_t buf);

/* The user must define this function.  A page should be made writable. */
error_t
pager_unlock_page (struct user_pager_info *pager,
		   vm_offset_t address);

/* The user must define this function.  It is used when you want be
   able to change association of pages to backing store.  To use it,
   pass non-zero value in NOTIFY_ON_EVICT when pager is created with
   pager_create.  You can change association of page only when
   pager_notify_evict has been called and you haven't touched page
   content after that.  Note there is a possibility that a page is
   evicted, but user is not notified about that.  The user should be
   able to handle this case.  */
void
pager_notify_evict (struct user_pager_info *pager,
		    vm_offset_t page);

/* The user must define this function.  It should report back (in
   *OFFSET and *SIZE the minimum valid address the pager will accept
   and the size of the object.   */
error_t
pager_report_extent (struct user_pager_info *pager,
		     vm_address_t *offset,
		     vm_size_t *size);

/* The user must define this function.  This is called when a pager is
   being deallocated after all extant send rights have been destroyed.  */
void
pager_clear_user_data (struct user_pager_info *pager);

/* The use must define this function.  This will be called when the ports
   library wants to drop weak references.  The pager library creates no
   weak references itself.  If the user doesn't either, then it's OK for
   this function to do nothing.  */
void
pager_dropweak (struct user_pager_info *p);

#endif
