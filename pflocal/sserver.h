/* Server for socket ops

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#ifndef __SSERVER_H__
#define __SSERVER_H__

/* Makes sure there are some request threads for sock operations, and starts
   a server if necessary.  This routine should be called *after* creating the
   port(s) which need server, as the server routine only operates while there
   are any ports.  */
void ensure_sock_server ();

/* A port bucket to handle SOCK_USERs and ADDRs.  */
struct port_bucket *sock_port_bucket;

#endif /* __SSERVER_H__ */
