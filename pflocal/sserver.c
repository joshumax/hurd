/* Server for socket ops

   Copyright (C) 1995, 1997, 2013 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <pthread.h>
#include <stdio.h>

#include <hurd/ports.h>

/* A port bucket to handle SOCK_USERs and ADDRs.  */
struct port_bucket *sock_port_bucket;

/* ---------------------------------------------------------------- */

/* True if there are threads servicing sock requests.  */
static int sock_server_active = 0;
static pthread_spinlock_t sock_server_active_lock = PTHREAD_SPINLOCK_INITIALIZER;

#include "io_S.h"
#include "fs_S.h"
#include "socket_S.h"
#include "../libports/interrupt_S.h"
#include "../libports/notify_S.h"

/* A demuxer for socket operations.  */
static int
sock_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = io_server_routine (inp)) ||
      (routine = fs_server_routine (inp)) ||
      (routine = socket_server_routine (inp)) ||
      (routine = ports_interrupt_server_routine (inp)) ||
      (routine = ports_notify_server_routine (inp)))
    {
      (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

/* Handle socket requests while there are sockets around.  */
static void *
handle_sock_requests (void *unused)
{
  while (ports_count_bucket (sock_port_bucket) > 0)
    {
      ports_enable_bucket (sock_port_bucket);
      ports_manage_port_operations_multithread (sock_port_bucket, sock_demuxer,
						30*1000, 2*60*1000, 0);
    }

  /* The last service thread is about to exist; make this known.  */
  pthread_spin_lock (&sock_server_active_lock);
  sock_server_active = 0;
  pthread_spin_unlock (&sock_server_active_lock);

  /* Let the whole joke start once again.  */
  ports_enable_bucket (sock_port_bucket);

  return NULL;
}

/* Makes sure there are some request threads for sock operations, and starts
   a server if necessary.  This routine should be called *after* creating the
   port(s) which need server, as the server routine only operates while there
   are any ports.  */
void
ensure_sock_server ()
{
  pthread_t thread;
  error_t err;

  pthread_spin_lock (&sock_server_active_lock);
  if (sock_server_active)
    pthread_spin_unlock (&sock_server_active_lock);
  else
    {
      sock_server_active = 1;
      pthread_spin_unlock (&sock_server_active_lock);
      err = pthread_create (&thread, NULL, handle_sock_requests, NULL);
      if (!err)
	pthread_detach (thread);
      else
	{
	  errno = err;
	  perror ("pthread_create");
	}
    }
}
