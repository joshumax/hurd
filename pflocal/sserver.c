/* Server for socket ops

   Copyright (C) 1995, 1997 Free Software Foundation, Inc.

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

#include <cthreads.h>

#include <hurd/ports.h>

/* A port bucket to handle SOCK_USERs and ADDRs.  */
struct port_bucket *sock_port_bucket;

/* ---------------------------------------------------------------- */

/* True if there are threads servicing sock requests.  */
static int sock_server_active = 0;
static spin_lock_t sock_server_active_lock = SPIN_LOCK_INITIALIZER;

/* A demuxer for socket operations.  */
static int
sock_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  extern int socket_server (mach_msg_header_t *inp, mach_msg_header_t *outp);
  extern int io_server (mach_msg_header_t *inp, mach_msg_header_t *outp);
  return
    socket_server (inp, outp)
      || io_server (inp, outp)
      || ports_interrupt_server (inp, outp)
      || ports_notify_server (inp, outp);
}

/* Handle socket requests while there are sockets around.  */
static void
handle_sock_requests ()
{
  while (ports_count_bucket (sock_port_bucket) > 0)
    {
      ports_enable_bucket (sock_port_bucket);
      ports_manage_port_operations_multithread (sock_port_bucket, sock_demuxer,
						30*1000, 2*60*1000, 0);
    }

  /* The last service thread is about to exist; make this known.  */
  spin_lock (&sock_server_active_lock);
  sock_server_active = 0;
  spin_unlock (&sock_server_active_lock);

  /* Let the whole joke start once again.  */
  ports_enable_bucket (sock_port_bucket);
}

/* Makes sure there are some request threads for sock operations, and starts
   a server if necessary.  This routine should be called *after* creating the
   port(s) which need server, as the server routine only operates while there
   are any ports.  */
void
ensure_sock_server ()
{
  spin_lock (&sock_server_active_lock);
  if (sock_server_active)
    spin_unlock (&sock_server_active_lock);
  else
    {
      sock_server_active = 1;
      spin_unlock (&sock_server_active_lock);
      cthread_detach (cthread_fork ((cthread_fn_t)handle_sock_requests,
				    (any_t)0));
    }
}
