/* Pager creation
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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

static struct pager *
_pager_create (size_t size,
	       struct port_bucket *bucket,
	       boolean_t may_cache,
	       memory_object_copy_strategy_t copy_strategy,
	       boolean_t notify_on_evict)
{
  struct pager *p;

  errno = ports_create_port (_pager_class, bucket, sizeof *p + size, &p);
  if (errno)
    return 0;

  p->pager_state = NOTINIT;
  pthread_mutex_init (&p->interlock, NULL);
  pthread_cond_init (&p->wakeup, NULL);
  p->lock_requests = 0;
  p->attribute_requests = 0;
  p->may_cache = may_cache;
  p->copy_strategy = copy_strategy;
  p->notify_on_evict = notify_on_evict;
  p->memobjcntl = MACH_PORT_NULL;
  p->memobjname = MACH_PORT_NULL;
  p->noterm = 0;
  p->termwaiting = 0;
  p->pagemap = 0;
  p->pagemapsize = 0;

  return p;
}

/* Create and return a new pager with user info UPI.  */
struct pager *
pager_create (struct user_pager_info *upi,
	      struct port_bucket *bucket,
	      boolean_t may_cache,
	      memory_object_copy_strategy_t copy_strategy,
	      boolean_t notify_on_evict)
{
  struct pager *p;

  p = _pager_create (0, bucket, may_cache, copy_strategy, notify_on_evict);
  if (p)
    p->upi = upi;

  return p;
}

struct pager *
pager_create_alloc (size_t u_pager_size,
		    struct port_bucket *bucket,
		    boolean_t may_cache,
		    memory_object_copy_strategy_t copy_strategy,
		    boolean_t notify_on_evict)
{
  struct pager *p;

  p = _pager_create (u_pager_size, bucket, may_cache, copy_strategy,
		     notify_on_evict);
  if (p)
    p->upi = (struct user_pager_info *) ((char *) p + sizeof *p);

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
