/* Implementation of memory_object_lock_completed for pager library
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


/* A lock_request has finished.  Do our part of the wakeup
   process.  */
_pager_seqnos_memory_object_lock_completed (mach_port_t object,
					    mach_port_seqno_t seqno,
					    mach_port_t control,
					    vm_offset_t offset,
					    vm_size_t length)
{
  struct controlinfo *ci;
  struct lock_request *lr;
  int wakeup;

  if (!(ci = check_port_type (object, pager_port_type)))
    {
      printf ("Bad lock completed\n");
      return EOPNOTSUPP;
    }

  if (control != ci->memobjcntl)
    {
      printf ("lock_completed: bad control port\n");
      return EPERM;
    }

  mach_port_deallocate (mach_task_self (), control);

  mutex_lock (&ci->interlock);
  _pager_wait_for_seqno (ci, seqno);

  wakeup = 0;
  for (lr = ci->lock_requests; lr; lr = lr->next)
    if (lr->start == offset && lr->end == offset + length
	&& !--lr->locks_pending && !lr->pending_writes)
      wakeup = 1;
  condition_broadcast (&ci->wakeup);

  _pager_release_seqno (ci);
  mutex_unlock (&ci->interlock);

  return 0;
}
