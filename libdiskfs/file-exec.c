/*
   Copyright (C) 1993, 1994, 1995, 1996, 1997 Free Software Foundation

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
#include <sys/stat.h>
#include <fcntl.h>
#include <hurd/exec.h>
#include <hurd/paths.h>
#include <string.h>
#include <idvec.h>

kern_return_t 
diskfs_S_file_exec (struct protid *cred,
		    task_t task,
		    int flags,
		    char *argv,
		    u_int argvlen,
		    char *envp,
		    u_int envplen,
		    mach_port_t *fds,
		    u_int fdslen,
		    mach_port_t *portarray,
		    u_int portarraylen,
		    int *intarray,
		    u_int intarraylen,
		    mach_port_t *deallocnames,
		    u_int deallocnameslen,
		    mach_port_t *destroynames,
		    u_int destroynameslen)
{
  struct node *np;
  uid_t uid;
  gid_t gid;
  mode_t mode;
  int suid, sgid;
  struct protid *newpi;
  error_t err = 0;
  
  if (!cred)
    return EOPNOTSUPP;

  if (diskfs_exec == MACH_PORT_NULL)
    diskfs_exec = file_name_lookup (_SERVERS_EXEC, 0, 0);
  if (diskfs_exec == MACH_PORT_NULL)
    return EOPNOTSUPP;
  
  np = cred->po->np;

  mutex_lock (&np->lock);
  mode = np->dn_stat.st_mode;
  uid = np->dn_stat.st_uid;
  gid = np->dn_stat.st_uid;
  mutex_unlock (&np->lock);

  if ((cred->po->openstat & O_EXEC) == 0)
    return EBADF;
  
  if (!((mode & (S_IXUSR|S_IXGRP|S_IXOTH))
	|| ((mode & S_IUSEUNK) && (mode & (S_IEXEC << S_IUNKSHIFT)))))
    return EACCES;
  
  if ((mode & S_IFMT) == S_IFDIR)
    return EACCES;

  suid = mode & S_ISUID;
  sgid = mode & S_ISGID;
  if (suid || sgid)
    {
      int secure = 0;
      error_t get_file_ids (struct idvec *uids, struct idvec *gids)
	{
	  error_t err = idvec_merge (uids, cred->user->uids);
	  if (! err)
	    err = idvec_merge (gids, cred->user->gids);
	  return err;
	}
      err =
	fshelp_exec_reauth (suid, uid, sgid, gid,
			    diskfs_auth_server_port, get_file_ids,
			    portarray, portarraylen, fds, fdslen, &secure);
      if (secure)
	flags |= EXEC_SECURE | EXEC_NEWTASK;
    }

  /* If the user can't read the file, then we should use a new task,
     which would be inaccessible to the user.  Actually, this doesn't
     work, because the proc server will still give out the task port
     to the user.  Too many things depend on that that it can't be
     changed.  So this vague attempt isn't even worth trying.  */
#if 0
  if (fshelp_access (&np->dn_stat, S_IREAD, cred->user))
    flags |= EXEC_NEWTASK;
#endif

  if (! err)
    err = diskfs_create_protid (diskfs_make_peropen (np, O_READ, cred->po),
				cred->user, &newpi);

  if (! err)
    {
      err = exec_exec (diskfs_exec, 
		       ports_get_right (newpi),
		       MACH_MSG_TYPE_MAKE_SEND,
		       task, flags, argv, argvlen, envp, envplen, 
		       fds, MACH_MSG_TYPE_COPY_SEND, fdslen,
		       portarray, MACH_MSG_TYPE_COPY_SEND, portarraylen,
		       intarray, intarraylen, deallocnames, deallocnameslen,
		       destroynames, destroynameslen);
      ports_port_deref (newpi);
    }

  if (! err)
    {
      unsigned int i;
      
      mach_port_deallocate (mach_task_self (), task);
      for (i = 0; i < fdslen; i++)
	mach_port_deallocate (mach_task_self (), fds[i]);
      for (i = 0; i < portarraylen; i++)
	mach_port_deallocate (mach_task_self (), portarray[i]);
    }

  return err; 
}
