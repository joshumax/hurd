/*
   Copyright (C) 1993, 1994 Free Software Foundation

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

/* Implement fsys_getroot as described in <hurd/fsys.defs>. */
error_t
diskfs_S_fsys_getroot (fsys_t controlport,
		       int flags,
		       uid_t *uids,
		       u_int nuids,
		       uid_t *gids,
		       u_int ngids,
		       file_t *result,
		       mach_msg_type_name_t *result_poly)
{
  struct port_info *pt = ports_check_port_type (controlport, PT_CTL);
  
  if (!pt)
    return EOPNOTSUPP;
  
  /* Check permission on flags. XXX */

  *result = (ports_get_right 
	     (diskfs_make_protid
	      (diskfs_make_peropen (diskfs_root_node, flags),
	       uids, nuids, gids, ngids)));
  *result_poly = MACH_MSG_TYPE_MAKE_SEND;

  ports_done_with_port (pt);

  return 0;
}
