/* 
   Copyright (C) 1994 Free Software Foundation

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
#include "fs_S.h"
#include "msg.h"

kern_return_t
diskfs_S_dir_notice_changes (struct protid *cred,
			     mach_port_t notify)
{
  struct dirmod *req;
  
  if (!cred)
    return EOPNOTSUPP;

  req = malloc (sizeof (struct dirmod));
  mutex_lock (&cred->po->np->lock);
  req->port = notify;
  req->next = cred->po->np->dirmod_reqs;
  cred->po->np->dirmod_reqs = req;
  nowait_dir_changed (notify, DIR_CHANGED_NULL, "");
  mutex_unlock (&cred->po->np->lock);
  return 0;
}

void
diskfs_notice_dirchange (struct node *dp, enum dir_changed_type type,
			 char *name)
{
  struct dirmod *req;
  
  for (req = dp->dirmod_reqs; req; req = req->next)
    nowait_dir_changed (req->port, type, name);
}  
