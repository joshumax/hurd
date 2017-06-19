/* Store backend using a Mach memory object
   Copyright (C) 2001, 2002 Free Software Foundation, Inc.

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

#include "store.h"
#include <hurd.h>
#include <hurd/sigpreempt.h>
#include <sys/mman.h>
#include <string.h>
#include <assert-backtrace.h>


/* Return a new store in STORE referring to the memory object MEMOBJ.
   Consumes the send right MEMOBJ.  */
error_t
store_memobj_create (memory_object_t memobj, int flags, size_t block_size,
		     const struct store_run *runs, size_t num_runs,
		     struct store **store)
{
  return _store_create (&store_memobj_class,
			memobj, flags, block_size, runs, num_runs, 0, store);
}


/* This one is pretty easy.  */
static error_t
memobj_map (const struct store *store, vm_prot_t prot, mach_port_t *memobj)
{
  *memobj = store->port;
  return mach_port_mod_refs (mach_task_self (), *memobj,
			     MACH_PORT_RIGHT_SEND, +1);
}


static error_t			/* swiped from libpager's pager_memcpy */
memobj_memcpy (memory_object_t memobj,
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
	  __sync_synchronize();

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
      assert_backtrace (scp->sc_error == EKERN_MEMORY_ERROR);
      err = EIO;
      to_copy -= sigcode - window;
      siglongjmp (buf, 1);
    }

  if (to_copy == 0)
    /* Short-circuit return if nothing to do.
       ERR would not be initialized by the copy loop in this case.  */
    return 0;

  if (sigsetjmp (buf, 1) == 0)
    hurd_catch_signal (sigmask (SIGSEGV) | sigmask (SIGBUS),
		       window, window + windowsize,
		       &copy, (sighandler_t) &fault);

  if (window)
    munmap ((caddr_t) window, windowsize);

  *size -= to_copy;

  return err;
}

static error_t
memobj_read (struct store *store,
	     store_offset_t addr, size_t index, size_t amount,
	     void **buf, size_t *len)
{
  addr <<= store->log2_block_size;
  if (((size_t) addr & (vm_page_size - 1)) == 0)
    {
      *len = amount;
      *buf = 0;
      return vm_map (mach_task_self (), (vm_address_t *) buf, amount,
		     0, 1, store->port, addr << store->log2_block_size, 0,
		     VM_PROT_READ, VM_PROT_ALL, VM_INHERIT_NONE);
    }
  else
    {
      error_t err;
      int alloced = 0;
      if (*len < amount)
	{
	  *buf = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  if (*buf == MAP_FAILED)
	    return errno;
	  alloced = 1;
	}
      *len = amount;
      err = memobj_memcpy (store->port, addr, *buf, len, VM_PROT_READ);
      if (err && alloced)
	munmap (*buf, amount);
      else if (alloced && round_page (*len) < round_page (amount))
	munmap (*buf + round_page (*len),
		round_page (amount) - round_page (*len));
      return err;
    }
}

static error_t
memobj_write (struct store *store,
	      store_offset_t addr, size_t index,
	      const void *buf, size_t len, size_t *amount)
{
  *amount = len;
  return memobj_memcpy (store->port, addr << store->log2_block_size,
			(void *) buf, amount, VM_PROT_WRITE);
}

static error_t
memobj_set_size (struct store *store, size_t newsize)
{
  return EOPNOTSUPP;
}

static error_t
memobj_decode (struct store_enc *enc, const struct store_class *const *classes,
	       struct store **store)
{
  return store_std_leaf_decode (enc, store_memobj_create, store);
}

const struct store_class
store_memobj_class =
{
  STORAGE_MEMORY, "memobj",
  map: memobj_map,
  read: memobj_read,
  set_size: memobj_set_size,
  write: memobj_write,
  allocate_encoding: store_std_leaf_allocate_encoding,
  encode: store_std_leaf_encode,
  decode: memobj_decode,
};
STORE_STD_CLASS (memobj);
