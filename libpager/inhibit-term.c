/* Locks to inhibit termination races for pager library
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

/* Prevent this memory object from being terminated. */
/* Must be called with interlock held. */
void
_pager_block_termination (struct pager *p)
{
  p->noterm++;
}

/* Allow termination again. */
/* Must be called with interlock held. */
void
_pager_allow_termination (struct pager *p)
{
  if (!--p->noterm && p->termwaiting)
    pthread_cond_broadcast (&p->wakeup);
}
