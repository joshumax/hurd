/* 
   Copyright (C) 1995,96,2001 Free Software Foundation, Inc.
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
  error_t err;
  struct idvec *uvec, *gvec;
  int i;
  struct protid *newpi;
  struct iouser *new_user;
  
  if (!user)
    return EOPNOTSUPP;
  
  if (idvec_contains (user->user->uids, 0))
    {
      err = iohelp_create_complex_iouser (&new_user, uids, nuids, gids, ngids);
      if (err)
        return err;
    }
  else
    {
      uvec = make_idvec ();
      if (! uvec)
        return ENOMEM;

      gvec = make_idvec ();
      if (! gvec)
        {
	  idvec_free (uvec);
	  return ENOMEM;
	}

      for (i = 0; i < user->user->uids->num; i++)
	if (listmember (uids, user->user->uids->ids[i], nuids))
	  {
	    err = idvec_add (uvec, user->user->uids->ids[i]);
	    if (err)
	      goto out;
	  }
      
      for (i = 0; i < user->user->gids->num; i++)
	if (listmember (gids, user->user->gids->ids[i], ngids))
	  {
	    err = idvec_add (gvec, user->user->gids->ids[i]);
	    if (err)
	      goto out;
	  }

      err = iohelp_create_iouser (&new_user, uvec, gvec);

      if (err)
        {
        out:
	  idvec_free (uvec);
	  idvec_free (gvec);
	  return err;
	}
    }
  
  mutex_lock (&user->po->np->lock);
  newpi = netfs_make_protid (user->po, new_user);
  if (newpi)
    {
      *newport = ports_get_right (newpi);
      mutex_unlock (&user->po->np->lock);
      *newporttype = MACH_MSG_TYPE_MAKE_SEND;
    }
  else
    {
      mutex_unlock (&user->po->np->lock);
      iohelp_free_iouser (new_user);
      err = ENOMEM;
    }
  
  ports_port_deref (newpi);
  return err;
}

