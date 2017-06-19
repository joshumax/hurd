/* Fault-safe copy into or out of pager-backed memory.
   Copyright (C) 1996,97,99, 2000,01,02 Free Software Foundation, Inc.
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

#include "priv.h"
#include "pager.h"
#include <sys/mman.h>
#include <hurd/sigpreempt.h>
#include <assert-backtrace.h>
#include <string.h>

/* Start using vm_copy over memcpy when we have that many page. This is
   roughly the L1 cache size.  (This value *cannot* be less than
   vm_page_size.) */
#define VMCOPY_BETTER_THAN_MEMCPY (8*vm_page_size)

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
  error_t err;
  size_t n = *size;

#define VMCOPY_WINDOW_DEFAULT_SIZE (32 * vm_page_size)
#define MEMCPY_WINDOW_DEFAULT_SIZE (32 * vm_page_size)
  vm_address_t window;
  vm_size_t window_size;

  error_t do_vm_copy (void)
    {
      assert_backtrace ((offset & (vm_page_size - 1)) == 0);
      assert_backtrace (((vm_address_t) other & (vm_page_size - 1)) == 0);
      assert_backtrace (n >= vm_page_size);

      do
	{
	  window_size =
	    VMCOPY_WINDOW_DEFAULT_SIZE > n
	    ? (n - (n & (vm_page_size - 1)))
	    : VMCOPY_WINDOW_DEFAULT_SIZE;

	  assert_backtrace (window_size >= VMCOPY_BETTER_THAN_MEMCPY);
	  assert_backtrace ((window_size & (vm_page_size - 1)) == 0);
	  
	  window = 0;
	  err = vm_map (mach_task_self (), &window, window_size, 0, 1,
			memobj, offset, 0, prot, prot, VM_INHERIT_NONE);
	  if (err)
	    return err;

	  if (prot == VM_PROT_READ)
	    err = vm_copy (mach_task_self (), window, window_size,
			   (vm_address_t) other);
	  else
	    err = vm_copy (mach_task_self (), (vm_address_t) other,
			   window_size, window);

	  vm_deallocate (mach_task_self (), window, window_size);

	  if (err)
	    return err;

	  other += window_size;
	  offset += window_size;
	  n -= window_size;
	}
      while (n >= VMCOPY_BETTER_THAN_MEMCPY);

      return 0;
    }

  error_t do_copy (struct hurd_signal_preemptor *preemptor)
    {
      error_t do_memcpy (size_t to_copy)
	{
	  window_size = MEMCPY_WINDOW_DEFAULT_SIZE;

	  do
	    {
	      size_t pageoff = offset & (vm_page_size - 1);
	      size_t copy_count = window_size - pageoff;

	      /* Map in and copy a standard-sized window, unless that is
		 more than the total left to be copied.  */

	      if (window_size >= round_page (pageoff + to_copy))
		{
		  copy_count = to_copy;
		  window_size = round_page (pageoff + to_copy);
		}

	      window = 0;
	      err = vm_map (mach_task_self (), &window, window_size, 0, 1,
			    memobj, offset - pageoff, 0,
			    prot, prot, VM_INHERIT_NONE);
	      if (err)
		return err;

	      /* Realign the fault preemptor for the new mapping window.  */
	      preemptor->first = window;
	      preemptor->last = window + window_size;
	      __sync_synchronize();

	      if (prot == VM_PROT_READ)
		memcpy (other, (const void *) window + pageoff, copy_count);
	      else
		memcpy ((void *) window + pageoff, other, copy_count);
	      
	      vm_deallocate (mach_task_self (), window, window_size);

	      offset += copy_count;
	      other += copy_count;
	      to_copy -= copy_count;
	      n -= copy_count;

	      assert_backtrace (n >= 0);
	      assert_backtrace (to_copy >= 0);
	    }
	  while (to_copy > 0);
	  
	  return 0;
	}

      /* Can we use vm_copy?  */
      if ((((vm_address_t) other & (vm_page_size - 1))
	   == (offset & (vm_page_size - 1)))
	  && (n >= (VMCOPY_BETTER_THAN_MEMCPY + vm_page_size
		    - ((vm_address_t) other & (vm_page_size - 1)))))
	/* 1) other and offset are aligned with repect to each other;
           and 2) we have at least VMCOPY_BETTER_THAN_MEMCPY fully
           aligned pages.  */
	{
	  err = do_memcpy (vm_page_size
			   - ((vm_address_t) other & (vm_page_size - 1)));
	  if (err)
	    return err;
   
	  assert_backtrace (n >= VMCOPY_BETTER_THAN_MEMCPY);

	  err = do_vm_copy ();
	  if (err || n == 0)
	    /* We failed or we finished.  */
	    return err;

	  assert_backtrace (n < VMCOPY_BETTER_THAN_MEMCPY);
	}

      return do_memcpy (n);
    }

  jmp_buf buf;
  void fault (int signo, long int sigcode, struct sigcontext *scp)
    {
      assert_backtrace (scp->sc_error == EKERN_MEMORY_ERROR);
      err = pager_get_error (pager, sigcode - window + offset);
      n -= sigcode - window;
      vm_deallocate (mach_task_self (), window, window_size);
      siglongjmp (buf, 1);
    }

  if (n == 0)
    /* Nothing to do.  */
    return 0;

  if (((vm_address_t) other & (vm_page_size - 1)) == 0
      && (offset & (vm_page_size - 1)) == 0
      && n >= VMCOPY_BETTER_THAN_MEMCPY)
    /* 1) the start address is page aligned; 2) the offset is page
       aligned; and 3) we have more than VMCOPY_BETTER_THAN_MEMCPY
       pages. */
    {
      err = do_vm_copy ();
      if (err || n == 0)
	/* We failed or we finished.  */
	{
	  *size -= n;
	  return err;
	}

      assert_backtrace (n < VMCOPY_BETTER_THAN_MEMCPY);
    }

  /* Need to do it the hard way.  */

  window = 0;
  window_size = 0;

  if (sigsetjmp (buf, 1) == 0)
    hurd_catch_signal (sigmask (SIGSEGV) | sigmask (SIGBUS),
		       window, window + window_size,
		       &do_copy, (sighandler_t) &fault);

  if (! err)
    assert_backtrace (n == 0);

  *size -= n;

  return err;
}
