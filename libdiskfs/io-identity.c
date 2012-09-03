/* libdiskfs implementation of io_identity RPC
   Copyright (C) 1996,97,98,2001,02 Free Software Foundation, Inc.
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

/* Implement io_identity as described in <hurd/io.defs>. */
error_t
diskfs_S_io_identity (struct protid *cred,
		      mach_port_t *id,
		      mach_msg_type_name_t *idtype,
		      mach_port_t *fsys,
		      mach_msg_type_name_t *fsystype,
		      ino_t *fileno)
{
  struct node *np;
  error_t err;
  ino_t inum;

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;
  pthread_mutex_lock (&np->lock);
  inum = np->dn_stat.st_ino;
  pthread_mutex_unlock (&np->lock);

  err = fshelp_get_identity (diskfs_port_bucket, inum, id);
  if (! err)
    {
      if (cred->po->shadow_root && cred->po->shadow_root != diskfs_root_node)
	{
	  err = fshelp_get_identity (diskfs_port_bucket,
				     cred->po->shadow_root->dn_stat.st_ino,
				     fsys);
	  if (err)
	    mach_port_deallocate (mach_task_self (), *id);
	}
      else
	*fsys = diskfs_fsys_identity;
    }
  if (! err)
    {
      *idtype = MACH_MSG_TYPE_MAKE_SEND;
      *fsystype = MACH_MSG_TYPE_MAKE_SEND;
      *fileno = inum;
    }

  return err;
}
