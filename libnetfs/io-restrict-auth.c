/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

#include "netfs.h"
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
  struct idvec *uvec, *gvec;
  int i;
  struct protid *newpi;
  
  if (!user)
    return EOPNOTSUPP;
  
  uvec = make_idvec ();
  gvec = make_idvec ();

  if (idvec_contains (user->user->uids, 0))
    {
      idvec_set_ids (uvec, uids, nuids);
      idvec_set_ids (gvec, gids, ngids);
    }
  else
    {
      for (i = 0; i < user->user->uids->num; i++)
	if (listmember (uids, user->user->uids->ids[i], nuids))
	  idvec_add (uvec, user->user->uids->ids[i]);
      
      for (i = 0; i < user->user->gids->num; i++)
	if (listmember (gids, user->user->gids->ids[i], ngids))
	  idvec_add (gvec, user->user->gids->ids[i]);
    }
  
  mutex_lock (&user->po->np->lock);
  newpi = netfs_make_protid (user->po, iohelp_create_iouser (uvec, gvec));
  *newport = ports_get_right (newpi);
  mutex_unlock (&user->po->np->lock);
  
  *newporttype = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (newpi);
  return 0;
}

