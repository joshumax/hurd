/* Fault-safe copy into or out of pager-backed memory.
   Copyright (C) 1996, 1997, 1999 Free Software Foundation, Inc.
   Written by Roland McGrath.

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

#include "pager.h"
#include <sys/mman.h>
#include <hurd/sigpreempt.h>
#include <assert.h>

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
	      vm_prot_t prot)
{
  vm_address_t window = 0;
  vm_size_t windowsize = 8 * vm_page_size;
  size_t to_copy = *size;
  error_t err;

  error_t copy (struct hurd_signal_preemptor *preemptor)
    {
      while (to_copy > 0)
	{
	  size_t pageoff = offset & (vm_page_size - 1);

	  if (window)
	    /* Deallocate the old window.  */
	    munmap ((caddr_t) window, windowsize);

	  /* Map in and copy a standard-sized window, unless that is
	     more than the total left to be copied.  */

	  if (windowsize > pageoff + to_copy)
	    windowsize = pageoff + to_copy;

	  window = 0;
	  err = vm_map (mach_task_self (), &window, windowsize, 0, 1,
			memobj, offset - pageoff, 0,
			prot, prot, VM_INHERIT_NONE);
	  if (err)
	    return 0;

	  /* Realign the fault preemptor for the new mapping window.  */
	  preemptor->first = window;
	  preemptor->last = window + windowsize;

	  if (prot == VM_PROT_READ)
	    memcpy (other, (const void *) window + pageoff,
		    windowsize - pageoff);
	  else
	    memcpy ((void *) window + pageoff, other, windowsize - pageoff);

	  offset += windowsize - pageoff;
	  other += windowsize - pageoff;
	  to_copy -= windowsize - pageoff;
	}
      return 0;
    }

  jmp_buf buf;
  void fault (int signo, long int sigcode, struct sigcontext *scp)
    {
      assert (scp->sc_error == EKERN_MEMORY_ERROR);
      err = pager_get_error (pager, sigcode - window + offset);
      to_copy -= sigcode - window;
      longjmp (buf, 1);
    }

  if (setjmp (buf) == 0)
    hurd_catch_signal (sigmask (SIGSEGV) | sigmask (SIGBUS),
		       window, window + windowsize,
		       &copy, (sighandler_t) &fault);

  if (window)
    munmap ((caddr_t) window, windowsize);

  *size -= to_copy;

  return err;
}
