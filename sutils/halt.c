/* Halt the system
   Copyright (C) 1994 Free Software Foundation, Inc.
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

#include <hurd.h>
#include <sys/reboot.h>
#include <hurd/startup.h>

main ()
{
  host_priv_t host_priv;
  device_t dev;
  process_t proc;
  mach_port_t msg;

  get_privileged_ports (&host_priv, &dev);
  proc = getproc ();
  proc_getmsgport (proc, 1, &msg);
  startup_reboot (msg, host_priv, RB_HALT);
}

  
  
  
