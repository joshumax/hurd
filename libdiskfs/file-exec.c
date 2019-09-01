/* File execution (file_exec_paths RPC) for diskfs servers, using exec server.
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 2000, 2002,
   2010 Free Software Foundation, Inc.

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
  return diskfs_S_file_exec_paths (cred,
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
diskfs_S_file_exec_paths (struct protid *cred,
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
  uid_t uid;
  gid_t gid;
  mode_t mode;
  int suid, sgid;
  struct protid *newpi;
  struct peropen *newpo;
  error_t err = 0;
  mach_port_t execserver;
  int cached_exec;
  struct hurd_userlink ulink;
  mach_port_t right;

#define RETURN(code) do { err = (code); goto out; } while (0)

  if (!cred)
    return EOPNOTSUPP;

  /* Get a light reference to the cached exec server port.  */
  execserver = _hurd_port_get (&_diskfs_exec_portcell, &ulink);
  cached_exec = (execserver != MACH_PORT_NULL);
  if (execserver == MACH_PORT_NULL)
    {
      /* No cached port.  Look up the canonical naming point.  */
      execserver = file_name_lookup (_SERVERS_EXEC, 0, 0);
      if (execserver == MACH_PORT_NULL)
	return EOPNOTSUPP;	/* No exec server, no exec.  */
      else
	{
	  /* Install the newly-gotten exec server port for other
	     threads to use, then get a light reference for this call.  */
	  _hurd_port_set (&_diskfs_exec_portcell, execserver);
	  execserver = _hurd_port_get (&_diskfs_exec_portcell, &ulink);
	}
    }

  np = cred->po->np;

  pthread_mutex_lock (&np->lock);
  mode = np->dn_stat.st_mode;
  uid = np->dn_stat.st_uid;
  gid = np->dn_stat.st_gid;
  pthread_mutex_unlock (&np->lock);

  if (_diskfs_noexec)
    RETURN (EACCES);

  if ((cred->po->openstat & O_EXEC) == 0)
    RETURN (EBADF);

  if (!((mode & (S_IXUSR|S_IXGRP|S_IXOTH))
	|| ((mode & S_IUSEUNK) && (mode & (S_IEXEC << S_IUNKSHIFT)))))
    RETURN (EACCES);

  if ((mode & S_IFMT) == S_IFDIR)
    RETURN (EACCES);

  suid = mode & S_ISUID;
  sgid = mode & S_ISGID;
  if (!_diskfs_nosuid && (suid || sgid))
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
    /* Make a new peropen for the exec server to access the file, since any
       seeking the exec server might want to do should not affect the original
       peropen on which file_exec_paths was called.  (The new protid
       for this peropen clones the caller's iouser to preserve the caller's
       authentication credentials.)  The new peropen's openmodes must have
       O_READ even if the caller had only O_EXEC privilege, so the exec
       server can read the executable file.  We also include O_EXEC so that
       the exec server can turn this peropen into a file descriptor in the
       target process and permit it to exec its /dev/fd/N pseudo-file.  */
    {
      err = diskfs_make_peropen (np, O_READ|O_EXEC, cred->po, &newpo);
      if (! err)
	{
	  err = diskfs_create_protid (newpo, cred->user, &newpi);
	  if (err)
	    diskfs_release_peropen (newpo);
	}
    }

  if (! err)
    {
      do
	{
	  right = ports_get_send_right (newpi);
#ifdef HAVE_EXEC_EXEC_PATHS
	  err = exec_exec_paths (execserver,
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
	    err = exec_exec (execserver,
			     right, MACH_MSG_TYPE_COPY_SEND,
			     task, flags, argv, argvlen, envp, envplen,
			     fds, MACH_MSG_TYPE_COPY_SEND, fdslen,
			     portarray, MACH_MSG_TYPE_COPY_SEND, portarraylen,
			     intarray, intarraylen,
			     deallocnames, deallocnameslen,
			     destroynames, destroynameslen);


	  mach_port_deallocate (mach_task_self (), right);
	  if (err == MACH_SEND_INVALID_DEST)
	    {
	      if (cached_exec)
		{
		  /* We were using a previously looked-up exec server port.
		     Try looking up a new one before giving an error.  */
		  cached_exec = 0;
		  _hurd_port_free (&_diskfs_exec_portcell, &ulink, execserver);

		  execserver = file_name_lookup (_SERVERS_EXEC, 0, 0);
		  if (execserver == MACH_PORT_NULL)
		    err = EOPNOTSUPP;
		  else
		    {
		      _hurd_port_set (&_diskfs_exec_portcell, execserver);
		      execserver = _hurd_port_get (&_diskfs_exec_portcell,
						   &ulink);
		    }
		}
	      else
		err = EOPNOTSUPP;
	    }
	} while (err == MACH_SEND_INVALID_DEST);
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

 out:
  _hurd_port_free (&_diskfs_exec_portcell, &ulink, execserver);

  return err;
}
