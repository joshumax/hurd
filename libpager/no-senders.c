/* Called when a nosenders notification happens
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


#include "priv.h"
#include <stdio.h>
#include <mach/notify.h>

/* The libpager library user must call this (and do nothing else) when
   a no-senders notification is received for a pager port.  P is pager
   on for which the notification was received; SEQNO and MSCOUNT are
   from the no-senders notification as described in <mach/notify.defs>. */
void
pager_no_senders (struct pager *p,
		  mach_port_seqno_t seqno,
		  mach_port_mscount_t mscount)
{
  mach_port_t old;
  int dealloc;
  
  mutex_lock (&p->interlock);
  _pager_wait_for_seqno (p, seqno);
  _pager_release_seqno (p);
  
  if (mscount > p->mscount)
    {
      printf ("pager strange no senders\n");
      dealloc = 1;
    }
  else if (mscount == p->mscount)
    dealloc = 1;
  else
    {
      /* Request a new notification.  The sync value is because we might
	 have accounted for a new sender but not actually made the send right
	 yet.  */
      mach_port_request_notification (mach_task_self (), p->port.port, 
				      MACH_NOTIFY_NO_SENDERS, p->mscount,
				      p->port.port, 
				      MACH_MSG_TYPE_MAKE_SEND_ONCE, &old);
      if (old)
	mach_port_deallocate (mach_task_self (), old);
      dealloc = 0;
    }

  mutex_unlock (&p->interlock);

  if (dealloc)
    ports_done_with_port (p);

  ports_done_with_port (p);		/* for previous check_port_type */
}


/* Called by port management routines when the last send-right
   to a pager has gone away.  This is a dual of pager_create.  */
void
pager_clean (void *arg)
{
  struct pager *p = arg;
  
  if (p->pager_state != NOTINIT)
    {
      mutex_lock (&p->interlock);
      _pager_free_structure (p);
    }
  
  pager_clear_user_data (p->upi);
}
