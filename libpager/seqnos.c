/* Sequence number synchronization routines for pager library
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

/* Wait until all preceding messages on this port have
   had a chance to be honored. */
void
_pager_wait_for_seqno (struct pager *p,
		int seqno)
{
  while (seqno != p->seqno + 1)
    {
      p->waitingforseqno = 1;
      condition_wait (&p->wakeup, &p->interlock);
    }
  p->seqno = seqno;
}

/* Allow the next message in. */
void
_pager_release_seqno (struct pager *p)
{
  if (p->waitingforseqno)
    {
      p->waitingforseqno = 0;
      condition_broadcast (&p->wakeup);
    }
}
