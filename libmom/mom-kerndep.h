/* Mach-specific type definitions for MOM
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

#include <mach/mach.h>
#include <spin-lock.h>

struct mom_port_ref
{
  mach_port_t port;
};

/* Mach-specific functions */

/* Initialize OBJ with a reference to Mach port PORT.  One Mach user
   reference is consumed.  */
error_t mom_mach_port_set (struct mom_port_ref *obj, mach_port_t port);

/* Return the Mach port corresponding to OBJ.  No new Mach user
   references are created, so this Mach port should not be used
   after OBJ has been destroyed. */
mach_port_t mom_fetch_mach_port (struct mom_port_ref *obj);

/* Turn a Mach error number into a Mom error number. */
mom_error_t mom_error_translate_mach (error_t macherr);
