/* Definitions for multi-threaded pager library
   Copyright (C) 1994,95,96,97,99, 2002 Free Software Foundation, Inc.

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

/* These declarations exists to place struct user_pager_info and
   struct pager in the proper scope.  */
struct user_pager_info;
struct pager;

struct pager_ops
{
  /* Read from PAGER's backing store, starting at page START, NPAGES
     pages.

     The data is to be provided using either pager_data_supply or
     pager_data_unavailable.

     If an error is encountered reading any pages, it is to be
     reported using pager_data_read_error.

     For each indicated page, the callee *must* call exactly one of
     the above methods; the pager library will not rerequest
     pages.  */
  void (*read)(struct pager *pager, struct user_pager_info *upi,
	       off_t start, off_t npages);

  /* Synchronously write to PAGER's backing store the NPAGES pages
     pointed to be BUF starting at page START.

     If DEALLOC is set, BUF must be deallocate be the callee.

     If an error is encountered while writing the pages to the backing
     store, it must be reported using pager_data_write_error.  */
  void (*write)(struct pager *pager, struct user_pager_info *upi,
		off_t start, off_t npages, void *buf, int dealloc);

  /* The NPAGES pages, starting at page START, should be made writable.

     Success is to be indicated using pager_data_unlock; errors using
     pager_data_unlock_error.  */
  void (*unlock)(struct pager *pager, struct user_pager_info *upi,
		 off_t start, off_t npages);

  /* Report the first (normally zero) and last valid pages that the
     pager will accept and store them in START and *END
     respectively.  */
  void (*report_extent)(struct user_pager_info *upi,
			   off_t *start, off_t *end);

  /* The user may define this function.  If non-NULL, it is called
     when a pager is being deallocated after all extant send rights
     have been destroyed.  */
  void (*clear_user_data)(struct user_pager_info *upi);

  /* This is called when the ports library wants to drop weak
     references.  The pager library creates no weak references itself.
     If the user doesn't either, then it's OK for this function to do
     nothing or be set to NULL.  */
  void (*dropweak)(struct user_pager_info *upi);
};

/* This de-muxer function is for use within libports_demuxer. */
/* INP is a message we've received; OUTP will be filled in with
   a reply message.  */
int pager_demuxer (mach_msg_header_t *inp,
		   mach_msg_header_t *outp);

/* Create a new pager.  The pager will have a port created for it (using
   libports, in BUCKET), but associated with the OPS operation structure
   and will be immediately ready to receive requests.  The pager will
   have one user reference created.  MAY_CACHE and COPY_STRATEGY are the
   original values of those attributes as for memory_object_ready.  
   Users may create references to pagers by use of the relevant ports
   library functions.  A block of memory of size UPI_SIZE for pager state
   will be allocated and provided to the call back functions or via
   pager_get_upi.  On errors, null is returned and sets errno is set.  */
struct pager *
pager_create (struct pager_ops *ops,
	      size_t upi_size,
	      struct port_bucket *bucket,
	      boolean_t may_cache,
	      memory_object_copy_strategy_t copy_strategy);

/* Return the user_pager_info struct associated with a pager. */
struct user_pager_info *
pager_get_upi (struct pager *pager);

/* Return the port (receive right) for requests to the pager.  It is
   absolutely necessary that a new send right be created from this
   receive right.  */
mach_port_t
pager_get_port (struct pager *pager);

/* Return the error code of the last page error for pager PAGER at
   page PAGE; this will be deleted when the kernel interface is
   fixed.  */
error_t
pager_get_error (struct pager *pager, off_t page);

/* Sync data from pager PAGER to backing store; wait for
   all the writes to complete iff WAIT is set. */
void
pager_sync (struct pager *pager,
	    int wait);

/* Sync some data (starting at page START, for NPAGES pages) from pager
   PAGER to backing store.  Wait for all the writes to complete iff
   WAIT is set.  */
void
pager_sync_some (struct pager *pager,
		 off_t start, off_t npages,
		 int wait);

/* Flush data from the kernel for pager PAGER and force any pending
   delayed copies.  Wait for all pages to be flushed iff WAIT is set. */
void
pager_flush (struct pager *pager,
	     int wait);

/* Flush some data (starting at page START, for NPAGES pages) for pager
   PAGER from the kernel.  Wait for all pages to be flushed iff WAIT
   is set.  */
void
pager_flush_some (struct pager *pager,
		  off_t start, off_t npages,
		  int wait);

/* Flush data from the kernel for pager PAGER and force any pending
   delayed copies.  Wait for all pages to be flushed iff WAIT is set.
   Have the kernel write back modifications.  */
void
pager_return (struct pager *pager,
	      int wait);

/* Flush some data (starting at page START, for NPAGES pages) for pager
   PAGER from the kernel.  Wait for all pages to be flushed iff WAIT
   is set.  Have the kernel write back modifications. */
void
pager_return_some (struct pager *pager,
		   off_t start, off_t npages,
		   int wait);

/* Offer the NPAGES pages from BUF to the kernel for pager PAGER
   starting at page START.  If PRECIOUS is set, then the pages will be
   paged out at some future point, otherwise they may be dropped with
   out notice.  IF READONLY is set, this data will be provided read
   only to the kernel.  In this case, any attempts to write to the
   pages will cause the PAGER->UNLOCK method to be called.  If DEALLOC
   is set, the buffer pointed to by BUF will be deallocated.

   NB: If the data is currently in core, the kernel may ignore this
   call.  As such, pager_flush_some should be called if the call was
   not in response to a PAGER->READ event.

   This function is normally called as a response to the PAGER->READ
   method.  */
void
pager_data_supply (struct pager *pager,
		   int precious, int readonly,
		   off_t start, off_t npages,
		   void *buf, int dealloc);

/* Indicate to the kernel that the NPAGES pages starting at START are
   unavailable and should be supplied as anonymous (i.e. zero)
   pages.

   This function is normally only called in response to the
   PAGER->READ method.  */
void
pager_data_unavailable (struct pager *pager,
			off_t start, off_t npages);

/* Indicate that an error has occured while trying to read the NPAGES
   pages starting at page START from pager PAGER's backing store.  The
   only permissable values for ERROR are: EIO, EDQUOT, and ENOSPC (all
   others will be ignored and squashed to EIO).

   This is normally only called in response to the PAGER->READ
   method.  */
void
pager_data_read_error (struct pager *pager,
		       off_t start, off_t npages,
		       error_t error);

/* Indicate that an error has occured while trying to write the NPAGES
   pages starting at page START to pager PAGER's backing store.  The
   only permissable values for ERROR are: EIO, EDQUOT, and ENOSPC (all
   others will be ignored and squashed to EIO).

   This is normally only called in response to the PAGER->WRITE
   method.  */
void
pager_data_write_error (struct pager *pager,
			off_t start, off_t npages,
			error_t error);

/* Indicate that the NPAGES pages starting at page START in pager PAGER
   have been made writable.

   This is normally only called in response to the PAGER->UNLOCK
   method.  */
void
pager_data_unlock (struct pager *pager,
		   off_t start, off_t npages);

/* Indicate that an error has occured unlocking (i.e. making writable)
   the NPAGES pages starting at page START in pager PAGER.  The only
   permissable values for ERROR are: EIO, EDQUOT, and ENOSPC (all
   others will be ignored and squashed to EIO).

   This is normally only called in response to the PAGER->UNLOCK
   method.  */
void
pager_data_unlock_error (struct pager *pager,
			 off_t start, off_t npages,
			 error_t error);

/* Change the attributes of the memory object underlying pager PAGER.
   Args MAY_CACHE and COPY_STRATEGY are as for
   memory_object_change_attributes.  Wait for the kernel to report
   completion if WAIT is set.*/
void
pager_change_attributes (struct pager *pager,
			 boolean_t may_cache,
			 memory_object_copy_strategy_t copy_strategy,
			 int wait);

/* Force termination of a pager.  After this returns, no
   more paging requests on the pager will be honored, and the
   pager will be deallocated.  (The actual deallocation might
   occur asynchronously if there are currently outstanding paging
   requests that will complete first.)  */
void
pager_shutdown (struct pager *pager);

/* Try to copy *SIZE bytes between the region OTHER points to and the
   region at byte OFFSET in the pager indicated by PAGER and MEMOBJ.
   If PROT is VM_PROT_READ, copying is from the pager to OTHER; if
   PROT contains VM_PROT_WRITE, copying is from OTHER into the pager.
   *SIZE is always filled in the actual number of bytes successfully
   copied.  Returns an error code if the pager-backed memory faults;
   if there is no fault, returns 0 and *SIZE will be unchanged.  */
error_t
pager_memcpy (struct pager *pager, memory_object_t memobj,
	      off_t offset, void *other, size_t *size,
	      vm_prot_t prot);

#endif
