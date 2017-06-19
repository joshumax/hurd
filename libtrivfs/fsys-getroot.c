/*
   Copyright (C) 1993,94,95,97,99,2002 Free Software Foundation, Inc.

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
#include "fsys_reply_U.h"
#include "trivfs_fsys_S.h"
#include <assert-backtrace.h>
#include <fcntl.h>
#include <string.h>

kern_return_t
trivfs_S_fsys_getroot (struct trivfs_control *cntl,
		       mach_port_t reply_port,
		       mach_msg_type_name_t reply_port_type,
		       mach_port_t dotdot,
		       uid_t *uids, size_t nuids,
		       uid_t *gids, size_t ngids,
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

  if (!cntl)
    return EOPNOTSUPP;

  if (trivfs_getroot_hook)
    {
      err = (*trivfs_getroot_hook) (cntl, reply_port, reply_port_type, dotdot,
				    uids, nuids, gids, ngids, flags,
				    do_retry, retry_name, newpt, newpttype);
      if (err != EAGAIN)
	return err;
    }

  if ((flags & O_WRITE & trivfs_allow_open) != (flags & O_WRITE))
    return EROFS;
  if ((flags & (O_READ|O_WRITE|O_EXEC) & trivfs_allow_open)
      != (flags & (O_READ|O_WRITE|O_EXEC)))
    return EACCES;

  /* O_CREAT and O_EXCL are not meaningful here; O_NOLINK and O_NOTRANS
     will only be useful when trivfs supports translators (which it doesn't
     now). */
  flags &= O_HURD;
  flags &= ~(O_CREAT|O_EXCL|O_NOLINK|O_NOTRANS);

  struct idvec idvec = {
    .ids = uids,
    .num = nuids,
    .alloced = nuids,
  };

  if (_is_privileged (&idvec))
    /* Privileged users should be given all our rights.  */
    err = io_duplicate (cntl->underlying, &new_realnode);
  else
    /* Non-privileged, restrict rights.  */
    err = io_restrict_auth (cntl->underlying,
			    &new_realnode, uids, nuids, gids, ngids);

  if (err)
    return err;

  err = iohelp_create_complex_iouser (&user, uids, nuids, gids, ngids);
  if (err)
    return err;

  /* Validate permissions.  */
  if (! trivfs_check_access_hook)
    file_check_access (new_realnode, &perms);
  else
    (*trivfs_check_access_hook) (cntl, user, new_realnode, &perms);
  if ((flags & (O_READ|O_WRITE|O_EXEC) & perms)
      != (flags & (O_READ|O_WRITE|O_EXEC)))
    err = EACCES;

  if (!err && trivfs_check_open_hook)
    err = (*trivfs_check_open_hook) (cntl, user, flags);
  if (!err)
    {
      if (! trivfs_open_hook)
	{
	  err = trivfs_open (cntl, user, flags, new_realnode, &cred);
	  if (!err)
	    mach_port_deallocate (mach_task_self (), dotdot);
	}
      else
	err = (*trivfs_open_hook) (cntl, user, dotdot, flags, new_realnode,
				   &cred);
    }

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
    }

  return err;
}
