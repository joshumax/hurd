/* fsys_forward

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include "priv.h"
#include "fsys_S.h"

/* Ask SERVER to provide fsys translation service for us.  REQUESTOR is
   the bootstrap port supplied to the original translator, and ARGV are
   the command line arguments.  If the recipient accepts the request, he
   (or some delegate) should send fsys_startup to REQUESTOR to start the
   filesystem up.  */
error_t
diskfs_S_fsys_forward (mach_port_t server,
		       mach_port_t reply, mach_msg_type_name_t reply_type,
		       mach_port_t requestor, data_t argz, size_t argz_len)
{
  return EOPNOTSUPP;
}
