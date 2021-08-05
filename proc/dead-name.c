/* Handle notifications
   Copyright (C) 1992, 1993, 1994, 1996, 1999, 2021
   Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell and Sergey Bugaev.  */


#include <hurd/ports.h>
#include "proc.h"

/* We ask for dead name notifications to detect when tasks and
   message ports die.  All notifications get sent to the notify
   port.  */
void
ports_dead_name (void *notify, mach_port_t dead_name)
{
  struct proc *p;

  check_dead_execdata_notify (dead_name);

  p = task_find_nocreate (dead_name);
  if (p)
    process_has_exited (p);

  ports_interrupt_notified_rpcs (notify, dead_name, MACH_NOTIFY_DEAD_NAME);
}
