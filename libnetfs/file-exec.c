/*
   Copyright (C) 1996, 1997, 2000, 2001, 2002, 2010
   Free Software Foundation, Inc.
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

/* Written by Michael I. Bushnell, p/BSG.  */

#include "netfs.h"
#include "execserver.h"
#include "fs_S.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <hurd/exec.h>
#include <hurd/paths.h>
#include <string.h>
#include <idvec.h>

kern_return_t
netfs_S_file_exec (struct protid *cred,
		   task_t task,
		   int flags,
		   data_t argv,
		   size_t argvlen,
		   data_t envp,
		   size_t envplen,
		   mach_port_t *fds,
		   size_t fdslen,
		   mach_port_t *portarray,
		   size_t portarraylen,
		   int *intarray,
		   size_t intarraylen,
		   mach_port_t *deallocnames,
		   size_t deallocnameslen,
		   mach_port_t *destroynames,
		   size_t destroynameslen)
{
  return netfs_S_file_exec_paths (cred,
				  task,
				  flags,
				  "",
				  "",
				  argv, argvlen,
				  envp, envplen,
				  fds, fdslen,
				  portarray, portarraylen,
				  intarray, intarraylen,
				  deallocnames, deallocnameslen,
				  destroynames, destroynameslen);
}

kern_return_t
netfs_S_file_exec_paths (struct protid *cred,
			 task_t task,
			 int flags,
			 char *path,
			 char *abspath,
			 char *argv,
			 size_t argvlen,
			 char *envp,
			 size_t envplen,
			 mach_port_t *fds,
			 size_t fdslen,
			 mach_port_t *portarray,
			 size_t portarraylen,
			 int *intarray,
			 size_t intarraylen,
			 mach_port_t *deallocnames,
			 size_t deallocnameslen,
			 mach_port_t *destroynames,
			 size_t destroynameslen)
{
  struct node *np;
  error_t err;
  uid_t uid;
  gid_t gid;
  mode_t mode;
  int suid, sgid;
  mach_port_t right;

  if (!cred)
    return EOPNOTSUPP;

  if (_netfs_exec == MACH_PORT_NULL)
    _netfs_exec = file_name_lookup (_SERVERS_EXEC, 0, 0);
  if (_netfs_exec == MACH_PORT_NULL)
    return EOPNOTSUPP;

  if ((cred->po->openstat & O_EXEC) == 0)
    return EBADF;

  np = cred->po->np;

  pthread_mutex_lock (&np->lock);
  mode = np->nn_stat.st_mode;
  uid = np->nn_stat.st_uid;
  gid = np->nn_stat.st_gid;
  err = netfs_validate_stat (np, cred->user);
  pthread_mutex_unlock (&np->lock);

  if (err)
    return err;

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
      error_t get_file_ids (struct idvec *uidsvec, struct idvec *gidsvec)
	{
	  error_t err;

	  err = idvec_merge (uidsvec, cred->user->uids);
	  if (! err)
	    err = idvec_merge (gidsvec, cred->user->gids);

	  return err;
	}
      err =
	fshelp_exec_reauth (suid, uid, sgid, gid,
			    netfs_auth_server_port, get_file_ids,
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
  if (diskfs_access (np, S_IREAD, cred))
    flags |= EXEC_NEWTASK;
#endif

  if (! err)
    {
      struct iouser *user;
      struct protid *newpi;

      err = iohelp_dup_iouser (&user, cred->user);
      if (! err)
        {
	  newpi = netfs_make_protid (netfs_make_peropen (np, O_READ, cred->po),
				     user);
	  if (newpi)
	    {
	      right = ports_get_send_right (newpi);
#ifdef HAVE_EXEC_EXEC_PATHS
	      err = exec_exec_paths (_netfs_exec,
				     right, MACH_MSG_TYPE_COPY_SEND,
				     task, flags, path, abspath,
				     argv, argvlen, envp, envplen,
				     fds, MACH_MSG_TYPE_COPY_SEND, fdslen,
				     portarray, MACH_MSG_TYPE_COPY_SEND,
				     portarraylen,
				     intarray, intarraylen,
				     deallocnames, deallocnameslen,
				     destroynames, destroynameslen);
	      /* For backwards compatibility.  Just drop it when we kill
		 exec_exec.  */
	      if (err == MIG_BAD_ID)
#endif
		err = exec_exec (_netfs_exec,
				 right, MACH_MSG_TYPE_COPY_SEND,
				 task, flags, argv, argvlen, envp, envplen,
				 fds, MACH_MSG_TYPE_COPY_SEND, fdslen,
				 portarray, MACH_MSG_TYPE_COPY_SEND,
				 portarraylen,
				 intarray, intarraylen,
				 deallocnames, deallocnameslen,
				 destroynames, destroynameslen);

	      mach_port_deallocate (mach_task_self (), right);
	      ports_port_deref (newpi);
	    }
	  else
	    {
	      err = errno;
	      iohelp_free_iouser (user);
	    }
	}
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
