/* Error codes for MOM library
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


/* MOM uses Mach error system 0x11 and subsystem 0. */
#define _MOM_ERRNO(n)  ((0x11 << 26 | ((n) & 0x3fff)))

enum __momerrors_error_codes
{
  /* These are standard errors to be returned by RPC user stubs
     in Mom systems.  */
     
  /* All Mom systems must detect and return these errors */

  /* The RPC attempted to send to an invalid mom_port_ref.  This can
     happen because, for example, the server it spoke to has died. */
  EMOM_INVALID_DEST	= _MOM_ERRNO (1),
  
  /* The RPC attempted to send an invalid mom_port_ref in its content.
     This shall not happen if the server the reference is to has
     merely died. */
  EMOM_INVALID_REF	= _MOM_ERRNO (2),
  
  /* The server began processing the RPC, but at some point it died. */
  EMOM_SERVER_DIED	= _MOM_ERRNO (3)
};


