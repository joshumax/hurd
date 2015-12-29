/* Notifications of directory changes.
   Copyright (C) 1994, 1995, 1998, 2001 Free Software Foundation, Inc.

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
#include "fs_notify_U.h"

kern_return_t
diskfs_S_dir_notice_changes (struct protid *cred,
			     mach_port_t notify)
{
  error_t err;
  struct modreq *req;
  struct node *np;

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;
  pthread_mutex_lock (&np->lock);
  if (!S_ISDIR (np->dn_stat.st_mode))
    {
      pthread_mutex_unlock (&np->lock);
      return ENOTDIR;
    }
  err = dir_changed (notify, np->dirmod_tick, DIR_CHANGED_NULL, "");
  if (err)
    {
      pthread_mutex_unlock (&np->lock);
      return err;
    }
  req = malloc (sizeof (struct modreq));
  if (! req)
    {
      pthread_mutex_unlock (&np->lock);
      return ENOMEM;
    }
  req->port = notify;
  req->next = np->dirmod_reqs;
  np->dirmod_reqs = req;
  pthread_mutex_unlock (&np->lock);
  return 0;
}

void
diskfs_notice_dirchange (struct node *dp, enum dir_changed_type type,
			 const char *name)
{
  error_t err;
  struct modreq **preq;

  dp->dirmod_tick++;
  preq = &dp->dirmod_reqs;
  while (*preq)
    {
      struct modreq *req = *preq;
      err = dir_changed (req->port, dp->dirmod_tick, type, (char *) name);
      if (err && err != MACH_SEND_TIMED_OUT)
	{
	  /* Remove notify port.  */
	  *preq = req->next;
	  mach_port_deallocate (mach_task_self (), req->port);
	  free (req);
	}
      else
	preq = &req->next;
    }
}
