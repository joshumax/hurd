/*
   Copyright (C) 1993, 1994, 1995 Free Software Foundation

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
#include "fsys_reply_U.h"
#include <assert.h>
#include <fcntl.h>
#include <string.h>

kern_return_t
trivfs_S_fsys_getroot (struct trivfs_control *cntl,
		       mach_port_t reply_port,
		       mach_msg_type_name_t reply_port_type,
		       mach_port_t dotdot,
		       uid_t *uids, u_int nuids,
		       uid_t *gids, u_int ngids,
		       int flags,
		       retry_type *do_retry,
		       char *retry_name,
		       mach_port_t *newpt,
		       mach_msg_type_name_t *newpttype)
{
  int perms;
  error_t err = 0;
  mach_port_t new_realnode;
  struct trivfs_protid *cred;
  struct iouser *user;
  struct idvec *uvec, *gvec;

  if (!cntl)
    return EOPNOTSUPP;

  if ((flags & (O_READ|O_WRITE|O_EXEC) & trivfs_allow_open)
      != (flags & (O_READ|O_WRITE|O_EXEC)))
    return EOPNOTSUPP;

  /* O_CREAT and O_EXCL are not meaningful here; O_NOLINK and O_NOTRANS
     will only be useful when trivfs supports translators (which it doesn't 
     now). */
  flags &= O_HURD;
  flags &= ~(O_CREAT|O_EXCL|O_NOLINK|O_NOTRANS);

  err = io_restrict_auth (cntl->underlying,
			  &new_realnode, uids, nuids, gids, ngids);
  if (err)
    return err;

  file_check_access (new_realnode, &perms);
  if ((flags & (O_READ|O_WRITE|O_EXEC) & perms)
      != (flags & (O_READ|O_WRITE|O_EXEC)))
    err = EACCES;

  uvec = make_idvec ();
  gvec = make_idvec ();
  idvec_set_ids (uvec, uids, nuids);
  idvec_set_ids (gvec, gids, ngids);
  user = iohelp_create_iouser (uvec, gvec);
  
  if (!err && trivfs_check_open_hook)
    err = (*trivfs_check_open_hook) (cntl, user, flags);
  if (!err)
    err = trivfs_open (cntl, user, flags, new_realnode, &cred);

  if (err)
    {
      mach_port_deallocate (mach_task_self (), new_realnode);
      iohelp_free_iouser (user);
    }
  else
    {
      *do_retry = FS_RETRY_NORMAL;
      *retry_name = '\0';
      *newpt = ports_get_right (cred);
      *newpttype = MACH_MSG_TYPE_MAKE_SEND;
      ports_port_deref (cred);
      mach_port_deallocate (mach_task_self (), dotdot);
    }

  return err;
}
