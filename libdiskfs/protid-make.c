/*
   Copyright (C) 1994,95,96,2001 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "priv.h"
#include <string.h>
#include <assert-backtrace.h>

/* Build and return in CRED a protid which has no user identification, for
   peropen PO.  On success, consume a reference to PO.  */
error_t
diskfs_start_protid (struct peropen *po, struct protid **cred)
{
  error_t err =
    ports_create_port_noinstall (diskfs_protid_class, diskfs_port_bucket,
				 sizeof (struct protid), cred);
  if (! err)
    {
      /* Consume a reference to po.  */
      (*cred)->po = po;
      (*cred)->shared_object = MACH_PORT_NULL;
      (*cred)->mapped = 0;
    }
  return err;
}

/* Finish building protid CRED started with diskfs_start_protid;
   the user to install is USER. */
void
diskfs_finish_protid (struct protid *cred, struct iouser *user)
{
  error_t err;

  if (!user)
    err = iohelp_create_simple_iouser (&cred->user, 0, 0);
  else
    err = iohelp_dup_iouser (&cred->user, user);
  assert_perror_backtrace (err);

  err = mach_port_move_member (mach_task_self (), cred->pi.port_right, 
			       diskfs_port_bucket->portset);
  assert_perror_backtrace (err);
}

/* Create and return a protid for an existing peropen PO in CRED for
   USER.  On success, consume a reference to PO.  */
error_t
diskfs_create_protid (struct peropen *po, struct iouser *user,
		      struct protid **cred)
{
  error_t err = diskfs_start_protid (po, cred);
  if (! err)
    diskfs_finish_protid (*cred, user);
  return err;
}
