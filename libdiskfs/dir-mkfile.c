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

/* Implement dir_mkfile as described in <hurd/fs.defs>. */
error_t
diskfs_S_dir_mkfile (struct protid *cred,
		     int flags,
		     mode_t mode,
		     mach_port_t *newnode,
		     mach_msg_type_name_t *newnodetype)
{
  struct node *np;
  error_t err;

  if (!cred)
    return EOPNOTSUPP;
  np = cred->po->np;
  mutex_lock (&np->lock);
  if (!S_ISDIR (np->dn_stat.st_mode))
    {
      mutex_unlock (&np->lock);
      return ENOTDIR;
    }
  err = diskfs_access (np, S_IWRITE, cred);
  mutex_unlock (&np->lock);
  if (err)
    return err;
  
  mode &= ~(S_IFMT | S_ISPARE | S_ISVTX);
  mode |= S_IFREG;
  err = diskfs_create_node (0, 0, mode, &np, cred, 0);
  if (err)
    return err;
  
  *returned_port = (diskfs_make_protid (diskfs_make_peropen (np, flags),
					cred->uids, cred->nuids, 
					cred->gids, cred->ngids))->pi.port;
  *newnodetype = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}

  
