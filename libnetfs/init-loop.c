/* 
   Copyright (C) 1996, 1997 Free Software Foundation, Inc.
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

void
netfs_server_loop ()
{
  error_t err;

  do 
    {
      ports_manage_port_operations_multithread (netfs_port_bucket,
						netfs_demuxer,
						thread_timeout,
						server_timeout,
						0);
      err = netfs_shutdown (0);
    }
  while (err);
  
  exit (0);
}
