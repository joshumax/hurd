/* Implementation of memory_object_terminate for pager library
   Copyright (C) 1994 Free Software Foundation

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


/* Called by the kernel when a shutdown has finished. */
/* This is a dual of seqnos_memory_object_init. */
kern_return_t
_pager_seqnos_memory_object_terminate (mach_port_t object, 
				       mach_port_seqno_t seqno,
				       mach_port_t control,
				       mach_port_t name)
{
  struct pager *p;
  struct lock_request *lr;
  int wakeup;
  
  if (!(p = check_port_type (object, pager_port_type)))
    return EOPNOTSUPP;
  
  if (control != p->memobjcntl)
    {
      printf ("incg terminate: wrong control port");
      goto out;
    }
  if (name != p->memobjname)
    {
      printf ("incg terminate: wrong name port");
      goto out;
    }

  if (p->pager_type != FILE_DATA && p->pager_type != SINDIR)
    {
      printf ("unexpected m_o_terminate\n");
      goto out;
    }      

  mutex_lock (&p->interlock);

  _pager_wait_for_seqno (p, seqno);

  while (p->noterm)
    {
      p->termwaiting = 1;
      condition_wait (&p->wakeup, &p->interlock);
    }

  wakeup = 0;
  for (lr = p->lock_requests; lr; lr = lr->next)
    {
      lr->locks_pending = 0;
      if (!lr->pending_writes)
	wakeup = 1;
    }
  if (wakeup)
    condition_broadcast (&p->wakeup);

  mach_port_deallocate (mach_task_self (), control);
  mach_port_deallocate (mach_task_self (), name);

  /* Free the pagemap */
  if (p->pagemapsize)
    {
      vm_deallocate (mach_task_self (), (u_int)p->pagemap, p->pagemapsize);
      p->pagemapsize = 0;
      p->pagemap = 0;
    }
  
  p->pager_state = NOTINIT;
  _pager_release_seqno (p);

  mutex_unlock (&p->interlock);

  if (shutting_down)
    /* Drop the user reference rather than waiting for the user to do it. */
    done_with_port (p);

 out:
  done_with_port (p);
  return 0;
}

