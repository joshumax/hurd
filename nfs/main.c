/* 
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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

#include <hurd/netfs.h>
#include <sys/socket.h>
#include <stdio.h>
#include <device/device.h>
#include "nfs.h"

int
main ()
{
  mach_port_t bootstrap;
  static volatile int hold = 1;

  while (hold);
    
  bootstrap = task_get_bootstrap_port (mach_task_self (), &bootstrap);
  netfs_init ();
  
  main_udp_socket = socket (PF_INET, SOCK_DGRAM, 0);
  soft_mount_retries = 3;

  {
    mach_port_t host, dev_master, timedev, obj;
    errno = get_privileged_ports (&host, &dev_master);
    if (errno)
      {
	perror ("getting privileged ports");
	exit (1);
      }
    device_open (dev_master, 0, "time", &timedev);
    device_map (timedev, VM_PROT_READ, 0, 
		sizeof (mapped_time_value_t), &obj, 0);
    vm_map (mach_task_self (), (vm_address_t *)&mapped_time,
	    sizeof (mapped_time_value_t), 0, 1, obj, 0, 0, VM_PROT_READ,
	    VM_PROT_READ, VM_INHERIT_NONE);
    mach_port_deallocate (mach_task_self (), timedev);
    mach_port_deallocate (mach_task_self (), obj);
  }

  cthread_detach (cthread_fork ((cthread_fn_t) timeout_service_thread, 0));
  cthread_detach (cthread_fork ((cthread_fn_t) rpc_receive_thread, 0));
  
  netfs_root_node = mount_root ("/home/gd4", "duality.gnu.ai.mit.edu");
  hostname = "";

  if (!netfs_root_node)
    exit (1);
  
  netfs_startup (bootstrap, 0);
  
  for (;;)
    netfs_server_loop ();
}

