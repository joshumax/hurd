/*
   Copyright (C) 1994, 1995, 1996, 1997 Free Software Foundation

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
#include "fs_S.h"
#include <fcntl.h>

/* Implement dir_mkfile as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_dir_mkfile (struct protid *cred,
		     int flags,
		     mode_t mode,
		     mach_port_t *newnode,
		     mach_msg_type_name_t *newnodetype)
{
  struct node *dnp, *np;
  error_t err;
  struct protid *newpi;
  
  if (!cred)
    return EOPNOTSUPP;
  if (diskfs_check_readonly ())
    return EROFS;
  dnp = cred->po->np;
  mutex_lock (&dnp->lock);
  if (!S_ISDIR (dnp->dn_stat.st_mode))
    {
      mutex_unlock (&dnp->lock);
      return ENOTDIR;
    }
  err = fshelp_access (&dnp->dn_stat, S_IWRITE, cred->user);
  if (err)
    {
      mutex_unlock (&dnp->lock);
      return err;
    }
  
  mode &= ~(S_IFMT | S_ISPARE | S_ISVTX);
  mode |= S_IFREG;
  err = diskfs_create_node (dnp, 0, mode, &np, cred, 0);
  mutex_unlock (&dnp->lock);

  if (diskfs_synchronous)
    {
      diskfs_file_update (dnp, 1);
      diskfs_file_update (np, 1);
    }

  if (err)
    return err;
  
  flags &= (O_READ | O_WRITE | O_EXEC);
  err = diskfs_create_protid (diskfs_make_peropen (np, flags, 
						   cred->po->dotdotport,
						   cred->po->depth),
			      cred->user, &newpi);
  if (! err)
    {
      *newnode = ports_get_right (newpi);
      *newnodetype = MACH_MSG_TYPE_MAKE_SEND;
      ports_port_deref (newpi);
    }

  diskfs_nput (np);

  return err;
}

  
