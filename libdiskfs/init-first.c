/*
   Copyright (C) 1994,95,97,2001 Free Software Foundation

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
#include <stdlib.h>
#include <hurd/ports.h>

static int thread_timeout = 1000 * 60 * 2; /* two minutes */
static int server_timeout = 1000 * 60 * 10; /* ten minutes */


static void *
master_thread_function (void *demuxer)
{
  error_t err;

  do
    {
      ports_manage_port_operations_multithread (diskfs_port_bucket,
						(ports_demuxer_type) demuxer,
						thread_timeout,
						server_timeout,
						0);
      err = diskfs_shutdown (0);
    }
  while (err);

  exit (0);
  /* NOTREACHED */
  return NULL;
}

void
diskfs_spawn_first_thread (ports_demuxer_type demuxer)
{
  pthread_t thread;
  error_t err;

  err = pthread_create (&thread, NULL, master_thread_function, demuxer);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }
}
