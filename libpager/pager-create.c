/* Pager creation
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

struct pager *
pager_create (struct user_pager_info *upi)
{
  struct pager *p;
  
  p = allocate_port (sizeof (struct pager), pager_port_type);
  
  p->upi = upi;
  p->pager_state = NOTINIT;
  mutex_init (&p->interlock);
  condition_init (&p->wakeup);
  p->lock_requests = 0;
  p->memobjcntl = MACH_PORT_NULL;
  p->memobjname = MACH_PORT_NULL;
  p->mscount = 0;
  p->seqno = -1;
  p->noterm = 0;
  p->termwaiting = 0;
  p->waitingforseqno = 0;
  p->pagemap = 0;
  p->pagemapsize = 0;
  
  spin_lock (&pagerlistlock);
  p->next = _pager_all_pagers;
  p->prevp = &_pager_all_pagers;
  _pager_all_pagers = p;
  if (p->next)
    p->next->prevp = &p->next;
  spin_unlock (&pagerlistlock);
  return p;
}

  
