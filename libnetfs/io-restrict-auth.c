/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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

#include "priv.h"
#include "io_S.h"

static inline int
listmember (int *list, int query, int n)
{
  int i;
  for (i = 0; i < n; i++)
    if (list[i] == query)
      return 1;
  return 0;
}

error_t
netfs_S_io_restrict_auth (struct protid *user,
			  mach_port_t *newport,
			  mach_msg_type_name_t *newporttype,
			  uid_t *uids,
			  mach_msg_type_number_t nuids,
			  gid_t *gids,
			  mach_msg_type_number_t ngids)
{
  uid_t *newuids, *newgids, *olduids, *oldgids;
  int i, newnuids, newngids, oldnuids, oldngids;
  struct protid *newpi;
  
  if (!user)
    return EOPNOTSUPP;
  
  netfs_interpret_credential (user->credenital, &olduids, &oldnuids,
			      &oldgids, &oldngids);
  newuids = alloca (sizeof (uid_t) * oldnuids);
  newgids = alloca (sizeof (gid_t) * oldngids);
  for (i = newnuids = 0; i < oldnuids; i++)
    if (listmember (uids, olduids[i], nuids))
      newuids[newnuids++] = olduids[i];
  for (i = newngids = 0; i < oldngids; i++)
    if (listmember (gids, oldgids[i], ngids))
      newgids[newngids++] = oldgids[i];
  
  mutex_lock (&cred->po->np->lock);
  newpi = netfs_make_protid (user->po, 
			     netfs_make_credential (newuids, newnuids,
						    newgids, newngids));
  *newport = ports_get_right (newpi);
  mutex_unlock (&cred->po->np->lock);
  
  *newporttype = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (newpi);
  return 0;
}

