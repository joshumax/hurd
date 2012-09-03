/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "priv.h"

/* Called by port management routines when the last send-right
   to a pager has gone away.  This is a dual of pager_create.  */
void
_pager_clean (void *arg)
{
  struct pager *p = arg;
#ifdef KERNEL_INIT_RACE
  struct pending_init *i, *tmp;
#endif  

  if (p->pager_state != NOTINIT)
    {
      pthread_mutex_lock (&p->interlock);
      _pager_free_structure (p);
#ifdef KERNEL_INIT_RACE
      for (i = p->init_head; i; i = tmp)
	{
	  mach_port_deallocate (mach_task_self (), i->control);
	  mach_port_deallocate (mach_task_self (), i->name);
	  tmp = i->next;
	  free (i);
	}
#endif
      pthread_mutex_unlock (&p->interlock);
    }

  pager_clear_user_data (p->upi);
}
