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
#include <string.h>

kern_return_t
trivfs_S_io_duplicate (struct trivfs_protid *cred,
		       mach_port_t *newport,
		       mach_msg_type_name_t *newporttype)
{
  struct trivfs_protid *newcred;
  
  if (!cred)
    return EOPNOTSUPP;
  
  newcred = ports_allocate_port (sizeof (struct trivfs_protid), cred->pi.type);
  newcred->realnode = cred->realnode;
  newcred->isroot = cred->isroot;
  newcred->po = cred->po;
  newcred->po->refcnt++;
  newcred->uids = malloc (cred->nuids * sizeof (uid_t));
  newcred->gids = malloc (cred->ngids * sizeof (gid_t));
  bcopy (cred->uids, newcred->uids, cred->nuids * sizeof (uid_t));
  bcopy (cred->gids, newcred->gids, cred->ngids * sizeof (uid_t));
  newcred->nuids = cred->nuids;
  newcred->ngids = cred->ngids;
  mach_port_mod_refs (mach_task_self (), newcred->realnode, 
		      MACH_PORT_RIGHT_SEND, 1);

  newcred->hook = cred->hook;
  
  if (trivfs_protid_create_hook)
    (*trivfs_protid_create_hook) (newcred);

  *newport = ports_get_right (newcred);
  *newporttype = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}

