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

/* Tell if the array LIST (of size N) contains a member equal to QUERY. */
static inline int
listmember (int *list, int query, int n)
{
  int i;
  for (i = 0; i < n; i++)
    if (list[i] == query)
      return 1;
  return 0;
}

kern_return_t
trivfs_S_io_restrict_auth (struct trivfs_protid *cred,
			   mach_port_t *newport,
			   mach_msg_type_name_t *newporttype,
			   uid_t *uids, u_int nuids,
			   uid_t *gids, u_int ngids)
{
  struct trivfs_protid *newcred;
  int i;
  uid_t *newuids, *newgids;
  int newnuids, newngids;
  
  if (!cred)
    return EOPNOTSUPP;
  
  newuids = alloca (sizeof (uid_t) * cred->nuids);
  newgids = alloca (sizeof (uid_t) * cred->ngids);
  for (i = newnuids = 0; i < cred->nuids; i++)
    if (listmember (uids, cred->uids[i], nuids))
      newuids[newnuids++] = cred->uids[i];
  for (i = newngids = 0; i < cred->gids[i]; i++)
    if (listmember (gids, cred->gids[i], ngids))
      newgids[newngids++] = cred->gids[i];

  newcred = ports_allocate_port (sizeof (struct trivfs_protid), cred->pi.type);
  newcred->isroot = 0;
  newcred->po = cred->po;
  newcred->po->refcnt++;
  if (cred->isroot)
    {
      for (i = 0; i < nuids; i++)
	if (uids[i] == 0)
	  newcred->isroot = 1;
    }
  newcred->gids = malloc (newngids * sizeof (uid_t));
  newcred->uids = malloc (newnuids * sizeof (uid_t));
  bcopy (newuids, newcred->uids, newnuids * sizeof (uid_t));
  bcopy (newgids, newcred->gids, newngids * sizeof (uid_t));
  newcred->ngids = newngids;
  newcred->nuids = newnuids;

  io_restrict_auth (cred->realnode, &newcred->realnode, 
		    newuids, newnuids, newgids, newngids);
  
  if (trivfs_protid_create_hook)
    (*trivfs_protid_create_hook) (newcred);

  *newport = ports_get_right (newcred);
  *newporttype = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}
