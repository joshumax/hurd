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
trivfs_S_fsys_getroot (struct trivfs_control *cntl;
		       int flags,
		       uid_t *uids, u_int nuids,
		       uid_t *gids, u_int ngids,
		       mach_port_t *newpt,
		       mach_msg_type_name_t *newpttype)
{
  struct trivfs_protid *cred;
  int i;

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
			      cntl->protidtypes);
  cred->isroot = 0;
  for (i = 0; i < nuids; i++)
    if (uids[i] == 0)
      cred->isroot = 1;
  cred->uids = malloc (nuids * sizeof (uid_t));
  cred->gids = malloc (ngids * sizeof (uid_t));
  bcopy (uids, cred->uids, nuids * sizeof (uid_t));
  bcopy (gids, cred->gids, ngids * sizeof (uid_t));
  cred->nuids = nuids;
  cred->ngids = ngids;

  cred->po = malloc (sizeof (struct trivfs_peropen));
  cred->po->refcnt = 1;
  cred->po->cntl = cntl;
  ports_port_ref (cntl);
  if (trivfs_peropen_create_hook)
    (*trivfs_peropen_create_hook) (cred->po);

  io_restrict_auth (cred->po->cntl->underlying, &cred->realnode, 
		    uids, nuids, gids, ngids);
  if (trivfs_protid_create_hook)
    (*trivfs_protid_create_hook) (cred);

  *newpt = ports_get_right (cred);
  *newpttype = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}


