/*
   Copyright (C) 2008, 2021 Free Software Foundation, Inc.
   Written by Zheng Da.

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

#include <hurd/ports.h>
#include "vdev.h"

void
ports_dead_name (void *notify, mach_port_t dead_name)
{
  debug ("ports_dead_name is called\n");
  remove_dead_port_from_dev (dead_name);
  ports_interrupt_notified_rpcs (notify, dead_name, MACH_NOTIFY_DEAD_NAME);
}
