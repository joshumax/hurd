/* Default address deallocate function

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

#include <hurd/ports.h>

/* This routine may be provided by the user, in which case, it should be a
   function taking a non-NULL source address and deallocating it.  It
   defaults to calling ports_port_deref.  */
void pipe_dealloc_addr (void *addr)
{
  ports_port_deref (addr);
}
