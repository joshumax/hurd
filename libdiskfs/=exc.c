/* 
   Copyright (C) 1994, 1995 Free Software Foundation

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

/* List of preempters that are around. */
struct preempt_record
{
  struct preempt_record *next;
  struct hurd_signal_preempt preempter1, preempter2;
  struct pager *p;
  vm_address_t off;
  void *addr;
  long len;
};

static struct preempt_record *preempt_list;
static spin_lock_t preempt_list_lock = SPIN_LOCK_INITIALIZER;

/* This special signal handler is run anytime we have taken a fault on
   a page that we map (registered through
   diskfs_register_memory_fault_area).  What we do is just longjmp to the
   context saved on the stack by diskfs_catch_exception.  */
static void
special_segv_handler (int code, int subcode, int error)
{
  struct preempt_record *rec;
  struct thread_stuff *stack_record;

  /* SUBCODE is the address and ERROR is the error returned by the
     pager through the kernel.  But because there is an annoying
     kernel bug (or rather unimplemented feature) the errors are not
     actually passed back.  So we have to scan the list of preempt
     records and find the one that got us here, and then query the
     pager to find out what error it gave the kernel. */
  spin_lock (&preempt_list_lock);
  for (rec = preempt_list; rec; rec = rec->next)
    if ((void *)subcode >= rec->addr
	&& (void *)subcode < rec->addr + rec->len)
      break;
  assert (rec);
  spin_unlock (&preempt_list_lock);
  error = pager_get_error (rec->p, rec->off + (void *)subcode - rec->addr);
  
  /* Now look up the stack record left by diskfs_catch_exception, consuming it
     on the way */
  stack_record = (struct thread_stuff *) cthread_data (cthread_self ());
  assert (stack_record);
  cthread_set_data (cthread_self (), (any_t)stack_record->link);
  
  /* And return to it... */
  longjmp (&stack_record->buf, error);

  abort ();
}

/* Return a signal handler for a thread which has faulted inside a
   region registered as expecting such faults.  This routine runs
   inside the library's signal thread, and accordingly must be
   careful.  */
static sighandler_t
segv_preempter (thread_t thread, int signo,
		long int sigcode, int sigerror)
{
  /* Just assume that everything is cool (how could it not be?)
     and return our special handler above. */
  return special_segv_handler;
}

/* Mark the memory at ADDR continuing for LEN bytes as mapped from pager P
   at offset OFF.  Call when vm_map-ing part of the disk.  */
void
diskfs_register_memory_fault_area (struct pager *p,
				   vm_address_t off,
				   void *addr, 
				   long len)
{
  struct preempt_record *rec = malloc (sizeof (struct preempt_record));

  hurd_preempt_signals (&rec->preempter1, SIGSEGV, addr, addr + len,
			segv_preempter);
  hurd_preempt_signals (&rec->preempter2, SIGBUS, addr, addr + len,
			segv_preempter);
  rec->p = p;
  rec->off = off;
  rec->addr = addr;
  rec->len = len;

  spin_lock (&preempt_list_lock);
  rec->next = preempt_list;
  preempt_list = rec;
  spin_unlock (&preempt_list_lock);
}

/* Mark the memory at ADDR continuing for LEN bytes as no longer
   mapped from the disk.  Call when vm_unmap-ing part of the disk.  */
void
diskfs_unregister_memory_fault_area (void *addr,
				     long len)
{
  struct preempt_record *rec, **prevp;
  
  spin_lock (&preempt_list_lock);
  for (rec = preempt_list, prevp = &preempt_list; 
       rec; 
       rec = rec->next, prevp = &rec->next)
    if (rec->addr == addr && rec->len == len)
      {
	/* This is it, make it go away. */
	*prevp = rec->next;
	spin_unlock (&preempt_list_lock);
	hurd_unpreempt_signals (&rec->preempter1, SIGSEGV);
	hurd_unpreempt_signals (&rec->preempter2, SIGBUS);
	free (rec);
	return;
      }
  spin_unlock (&preempt_list_lock);
  
  /* Not found */
  assert (0);
}

/* Set up the exception handling system.  */
void
init_exceptions ()
{
}
