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
#include <assert.h>
#include <fcntl.h>

kern_return_t
trivfs_S_fsys_getroot (mach_port_t fsys,
		       int flags,
		       uid_t *uids, u_int nuids,
		       uid_t *gids, u_int ngids,
		       mach_port_t *newpt,
		       mach_msg_type_name_t *newpttype)
{
  struct trivfs_protid *cred;
  int i;
  struct trivfs_control *cntl;

  cntl = ports_check_port_type (fsys, trivfs_cntl_porttype);
  if (!cntl)
    return EOPNOTSUPP;

  assert (!trivfs_support_read && !trivfs_support_write
	  && !trivfs_support_exec);
  
  if (flags & (O_READ|O_WRITE|O_EXEC))
    {
      ports_done_with_port (cntl);
      return EACCES;
    }
  
  cred = ports_allocate_port (sizeof (struct trivfs_protid),
			      trivfs_protid_porttype);
  cred->isroot = 0;
  for (i = 0; i < nuids; i++)
    if (uids[i] == 0)
      cred->isroot = 1;
  cred->cntl = cntl;
  ports_port_ref (cntl);
  io_restrict_auth (cred->cntl->underlying, &cred->realnode, 
		    uids, nuids, gids, ngids);
  *newpt = ports_get_right (cred);
  *newpttype = MACH_MSG_TYPE_MAKE_SEND;
  ports_done_with_port (cntl);
  return 0;
}


