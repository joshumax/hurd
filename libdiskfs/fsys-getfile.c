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
#include "fsys_S.h"

error_t
diskfs_S_fsys_getfile (mach_port_t fsys __attribute__ ((unused)),
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       uid_t *gen_uids __attribute__ ((unused)),
		       u_int ngen_uids __attribute__ ((unused)),
		       uid_t *gen_gids __attribute__ ((unused)),
		       u_int ngen_gids __attribute__ ((unused)),
		       char *handle __attribute__ ((unused)),
		       u_int handlelen __attribute__ ((unused)),
		       mach_port_t *file __attribute__ ((unused)),
		       mach_msg_type_name_t *filetype __attribute__ ((unused)))
{
  return EOPNOTSUPP;
}

