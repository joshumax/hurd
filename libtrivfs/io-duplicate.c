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
#include "io_S.h"

kern_return_t
trivfs_S_io_duplicate (struct trivfs_protid *cred,
		       mach_port_t *newport,
		       mach_msg_type_name_t *newporttype)
{
  struct trivfs_protid *newcred;
  
  if (!cred)
    return EOPNOTSUPP;
  
  newcred = ports_allocate_port (sizeof (struct trivfs_protid), 
				 trivfs_protid_porttype);
  newcred->realnode = cred->realnode;
  newcred->isroot = cred->isroot;
  newcred->cntl = cred->cntl;
  ports_port_ref (newcred->cntl);
  mach_port_mod_refs (mach_task_self (), newcred->realnode, 
		      MACH_PORT_RIGHT_SEND, 1);
  *newport = ports_get_right (newcred);
  *newporttype = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}

