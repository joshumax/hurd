/* Pager creation
   Copyright (C) 1994,95,96,2001,02 Free Software Foundation

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

/* Create and return a new pager with user info UPI.  */
struct pager *
pager_create (struct pager_ops *ops,
	      size_t upi_size,
	      struct port_bucket *bucket,
	      boolean_t may_cache,
	      memory_object_copy_strategy_t copy_strategy)
{
  error_t err;
  struct pager *p;

  err = ports_create_port (_pager_class, bucket,
			   sizeof (struct pager) + upi_size, &p);
  if (err)
    return 0;

  p->ops = ops;
  p->pager_state = NOTINIT;
  mutex_init (&p->interlock);
  condition_init (&p->wakeup);
  p->lock_requests = 0;
  p->attribute_requests = 0;
  p->may_cache = may_cache;
  p->copy_strategy = copy_strategy;
  p->memobjcntl = MACH_PORT_NULL;
  p->memobjname = MACH_PORT_NULL;
  p->seqno = -1;
  p->noterm = 0;
  p->termwaiting = 0;
  p->waitingforseqno = 0;
  p->pagemap = 0;
  p->pagemapsize = 0;

  return p;
}


/* This causes the function to be run at startup by compiler magic. */
static void create_class (void) __attribute__ ((constructor));

static void
create_class ()
{
  _pager_class = ports_create_class (_pager_clean, _pager_real_dropweak);
  (void) &create_class;		/* Avoid warning */
}


