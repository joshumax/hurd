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

error_t
trivfs_S_io_restrict_auth (struct protid *cred,
			   mach_port_t *newport,
			   mach_msg_type_name_t *newporttype,
			   uid_t *uids, u_int nuids,
			   uid_t *gids, u_int ngids)
{
  struct protid *newcred;
  int i;
  
  if (!cred)
    return EOPNOTSUPP;
  
  newcred = ports_allocate_port (sizeof (struct protid), 
				 trivfs_protid_porttype);
  newcred->isroot = 0;
  newcred->cntl = cred->cntl;
  ports_port_ref (newcred->cntl);
  if (cred->isroot)
    {
      for (i = 0; i < nuids; i++)
	if (uids[i] == 0)
	  newcred->isroot = 1;
    }
  io_restrict_auth (cred->realnode, &newcred->realnode, 
		    uids, nuids, gids, ngids);
  
  *newport = ports_get_right (newcred);
  *newporttype = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}
