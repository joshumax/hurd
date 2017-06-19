/* Changing pager attributes synchronously
   Copyright (C) 1994, 1996 Free Software Foundation

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
#include <assert-backtrace.h>

/* Change the attributes of the memory object underlying pager P.
   Arguments MAY_CACHE and COPY_STRATEGY are as for
   memory_object_change_attributes.  Wait for the kernel to report
   completion if WAIT is set.  */
void
pager_change_attributes (struct pager *p,
 			 boolean_t may_cache,
 			 memory_object_copy_strategy_t copy_strategy,
			 int wait)
{
  struct attribute_request *ar = 0;
  
  pthread_mutex_lock (&p->interlock);

  /* If there's nothing to do we might be able to return.  However,
     if the user asked us to wait, and there are pending changes,
     then we have to do the work anyway because we must follow the
     pending change.  */
  if (p->may_cache == may_cache && p->copy_strategy == copy_strategy
      && ! (p->attribute_requests && wait))
    {
      pthread_mutex_unlock (&p->interlock);
      return;
    }

  p->may_cache = may_cache;
  p->copy_strategy = copy_strategy;
  
  if (p->pager_state == NOTINIT)
    {
      pthread_mutex_unlock (&p->interlock);
      return;
    }

  if (wait)
    {
      for (ar = p->attribute_requests; ar; ar = ar->next)
	if (ar->may_cache == may_cache 
	    && ar->copy_strategy == copy_strategy)
	  {
	    ar->attrs_pending++;
	    ar->threads_waiting++;
	    break;
	  }
      if (!ar)
	{
	  ar = malloc (sizeof (struct attribute_request));
	  ar->may_cache = may_cache;
	  ar->copy_strategy = copy_strategy;
	  ar->attrs_pending = 1;
	  ar->threads_waiting = 1;
	  ar->next = p->attribute_requests;
	  if (ar->next)
	    ar->next->prevp = &ar->next;
	  ar->prevp = &p->attribute_requests;
	  p->attribute_requests = ar;
	}
    }      

  pthread_mutex_unlock (&p->interlock);
  memory_object_change_attributes (p->memobjcntl, may_cache, copy_strategy,
				   wait ? p->port.port_right : MACH_PORT_NULL);
  
  if (wait)
    {
      pthread_mutex_lock (&p->interlock);

      while (ar->attrs_pending)
	pthread_cond_wait (&p->wakeup, &p->interlock);

      if (! --ar->threads_waiting)
	{
	  *ar->prevp = ar->next;
	  if (ar->next)
	    ar->next->prevp = ar->prevp;
	  free (ar);
	}

      pthread_mutex_unlock (&p->interlock);
    }
}
