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
#include "fs_S.h"
#include <sys/stat.h>
#include <fcntlbits.h>
#include <hurd/exec.h>

error_t 
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
  error_t err;
  
  if (!cred)
    return EOPNOTSUPP;
  
  np = cred->po->np;
  if (err = diskfs_access (np, S_IEXEC, cred))
    return err;
  if (!((np->dn_stat.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH))
	|| ((np->dn_stat.st_mode & S_IUSEUNK)
	    && (np->dn_stat.st_mode & (S_IEXEC << S_IUNKSHIFT)))))
    return EACCES;
  
  /* Handle S_ISUID and S_ISGID uid substitution. */
  /* XXX We should change the auth handle here too. */
  if (((np->dn_stat.st_mode & S_ISUID)
       && !diskfs_isuid (np->dn_stat.st_uid, cred))
      || ((np->dn_stat.st_mode & S_ISGID)
	  && !diskfs_groupmember (np->dn_stat.st_gid, cred)))
    flags |= EXEC_SECURE|EXEC_NEWTASK;

  if (diskfs_access (np, S_IREAD, cred))
    flags |= EXEC_NEWTASK;

  err = exec_exec (diskfs_exec, 
		   (ports_get_right 
		    (diskfs_make_protid
		     (diskfs_make_peropen (np, O_READ),
		      cred->uids, cred->nuids, cred->gids, cred->ngids))),
		   task, flags, argv, argvlen, envp, envplen, 
		   fds, MACH_MSG_TYPE_MOVE_SEND, fdslen,
		   portarray, MACH_MSG_TYPE_MOVE_SEND, portarraylen,
		   intarray, intarraylen, deallocnames, deallocnameslen,
		   destroynames, destroynameslen);
  mach_port_deallocate (mach_task_self (), task);
  return err;
}
