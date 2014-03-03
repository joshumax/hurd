/* Sequence number synchronization routines for pager library
   Copyright (C) 1994, 2011 Free Software Foundation

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
#include <assert.h>

/* The message with seqno SEQNO has just been dequeued for pager P;
   wait until all preceding messages have had a chance and then
   return.  */
void
_pager_wait_for_seqno (struct pager *p,
		       mach_port_seqno_t seqno)
{
  while (seqno != p->seqno + 1)
    {
      p->waitingforseqno = 1;
      pthread_cond_wait (&p->wakeup, &p->interlock);
    }
}


/* Allow the next message for pager P (potentially blocked in
   _pager_wait_for_seqno) to be handled.  */
void
_pager_release_seqno (struct pager *p,
		      mach_port_seqno_t seqno)
{
  assert (seqno == p->seqno + 1);
  p->seqno = seqno;
  if (p->waitingforseqno)
    {
      p->waitingforseqno = 0;
      pthread_cond_broadcast (&p->wakeup);
    }
}


/* Just update the seqno.  */
void
_pager_update_seqno (mach_port_t object,
                     mach_port_seqno_t seqno)
{
  struct pager *p;

  p = ports_lookup_port (0, object, _pager_class);
  _pager_update_seqno_p (p, seqno);
  if (p)
    ports_port_deref (p);
}


/* Just update the seqno, pointer version.  */
void
_pager_update_seqno_p (struct pager *p,
                       mach_port_seqno_t seqno)
{
  if (p
      && p->port.class == _pager_class)
    {
      pthread_mutex_lock (&p->interlock);
      _pager_wait_for_seqno (p, seqno);
      _pager_release_seqno (p, seqno);
      pthread_mutex_unlock (&p->interlock);
    }
}
