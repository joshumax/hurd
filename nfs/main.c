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
#include <netinet/in.h>
#include <unistd.h>
#include <maptime.h>

int stat_timeout = 3;
int cache_timeout = 3;
int initial_transmit_timeout = 1;
int max_transmit_timeout = 30;
int soft_retries = 3;
int mounted_soft = 1;
int read_size = 8192;
int write_size = 8192;

int
main ()
{
  mach_port_t bootstrap;
  static volatile int hold = 0;
  struct sockaddr_in addr;
  int ret;

  while (hold);
    
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  netfs_init ();
  
  main_udp_socket = socket (PF_INET, SOCK_DGRAM, 0);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons (IPPORT_RESERVED);
  do
    {
      addr.sin_port = htons (ntohs (addr.sin_port) - 1);
      ret = bind (main_udp_socket, (struct sockaddr *)&addr, 
		  sizeof (struct sockaddr_in));
      if (ret == -1 && errno == EPERM)
	{
	  /* We aren't allowed privileged ports; no matter;
	     let the server deny us later if it wants. */
	  ret = 0;
	  break;
	}
    }
  while ((ret == -1) && (errno == EADDRINUSE));
  if (ret == -1)
    {
      perror ("binding main udp socket");
      exit (1);
    }

  errno = maptime_map (0, 0, &mapped_time);
  if (errno)
    perror ("mapping time");

  cthread_detach (cthread_fork ((cthread_fn_t) timeout_service_thread, 0));
  cthread_detach (cthread_fork ((cthread_fn_t) rpc_receive_thread, 0));
  
  hostname = malloc (1000);
  gethostname (hostname, 1000);
  netfs_root_node = mount_root ("/home/gd4", "duality.gnu.ai.mit.edu");

  if (!netfs_root_node)
    exit (1);
  
  netfs_startup (bootstrap, 0);
  
  for (;;)
    netfs_server_loop ();
}

