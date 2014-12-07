/* Completion of memory_object_change_attributes
   Copyright (C) 1994, 1995 Free Software Foundation

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

/* The kernel calls this (as described in <mach/memory_object.defs>)
   when a memory_object_change_attributes call has completed.  Read this
   in combination with pager-attr.c.  */
kern_return_t
_pager_S_memory_object_change_completed (struct pager *p,
				       boolean_t maycache,
				       memory_object_copy_strategy_t strat)
{
  struct attribute_request *ar;

  if (!p
      || p->port.class != _pager_class)
    {
      printf ("Bad change completed\n");
      return EOPNOTSUPP;
    }
  
  pthread_mutex_lock (&p->interlock);

  for (ar = p->attribute_requests; ar; ar = ar->next)
    if (ar->may_cache == maycache && ar->copy_strategy == strat)
      {
	if (ar->attrs_pending && !--ar->attrs_pending)
	  pthread_cond_broadcast (&p->wakeup);
	break;
      }

  pthread_mutex_unlock (&p->interlock);
  return 0;
}
