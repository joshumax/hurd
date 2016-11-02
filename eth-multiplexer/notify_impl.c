/*
   Copyright (C) 2008 Free Software Foundation, Inc.
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

#include <hurd.h>
#include <mach.h>

#include "vdev.h"

/* Implementation of notify interface */
kern_return_t
do_mach_notify_port_deleted (struct port_info *pi,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_msg_accepted (struct port_info *pi,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_port_destroyed (struct port_info *pi,
			       mach_port_t port)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_no_senders (struct port_info *pi,
			   mach_port_mscount_t mscount)
{
  return ports_do_mach_notify_no_senders (pi, mscount);
}

kern_return_t
do_mach_notify_send_once (struct port_info *pi)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_dead_name (struct port_info *pi,
			  mach_port_t name)
{
  debug ("do_mach_notify_dead_name is called\n");
  mach_port_deallocate (mach_task_self (), name);
  remove_dead_port_from_dev (name);
  return 0;
}
