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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "fshelp.h"
#include <hurd/fsys.h>

error_t
fshelp_set_active (struct transbox *box,
		   mach_port_t active,
		   int excl)
{
  int cancel;
  
  if (excl 
      && ((box->active != MACH_PORT_NULL) || (box->flags & TRANSBOX_STARTING)))
    return EBUSY;
  
  while (box->flags & TRANSBOX_STARTING)
    {
      box->flags |= TRANSBOX_WANTED;
      cancel = hurd_condition_wait (&box->wakeup, box->lock);
      if (cancel)
	return EINTR;
    }

  if (box->active != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), box->active);

  box->active = active;
  return 0;
}

