/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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
#include <assert-backtrace.h>

error_t
fshelp_fetch_control (struct transbox *box,
		      mach_port_t *control)
{
  error_t err = 0;
  *control = box->active;
  if (*control != MACH_PORT_NULL)
    err = mach_port_mod_refs (mach_task_self (), *control,
                              MACH_PORT_RIGHT_SEND, 1);

  if (err == KERN_INVALID_RIGHT)
    *control = box->active = MACH_PORT_NULL;

  return err;
}
