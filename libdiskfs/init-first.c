/*
   Copyright (C) 1994, 1995 Free Software Foundation

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

/* Written by Michael I. Bushnell.  */

#include "priv.h"

#define THREAD_TIMEOUT 1000 * 60 * 2  /* two minutes */
#define SERVER_TIMEOUT 1000 * 60 * 10 /* ten minutes */

static void
master_thread_function (any_t foo __attribute__ ((unused)))
{
  error_t err;

  do 
    {
      ports_manage_port_operations_multithread (diskfs_port_bucket,
						diskfs_demuxer,
						THREAD_TIMEOUT,
						SERVER_TIMEOUT,
						1, MACH_PORT_NULL);
      err = diskfs_shutdown (0);
    }
  while (err);
  
  /* Successful diskfs_shutdown should never return. */
  abort ();
}

void
diskfs_spawn_first_thread (void)
{
  cthread_detach (cthread_fork ((cthread_fn_t) master_thread_function,
				(any_t) 0));
}
