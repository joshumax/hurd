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

#include "netfs.h"

static int thread_timeout = 1000 * 60 * 2; /* two minutes */
static int server_timeout = 1000 * 60 * 10; /* ten minutes */


static any_t
master_thread_function (any_t foo __attribute__ ((unused)))
{
  error_t err;

  do 
    {
      ports_manage_port_operations_multithread (diskfs_port_bucket,
						diskfs_demuxer,
						thread_timeout,
						server_timeout,
						1, MACH_PORT_NULL);
      err = diskfs_shutdown (0);
    }
  while (err);
  
  exit (0);
}

void
netfs_server_loop (void)
{
  cthread_detach (cthread_fork ((cthread_fn_t) master_thread_function,
				(any_t) 0));
}
  
