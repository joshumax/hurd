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
#include <hurd/paths.h>

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
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  if (diskfs_exec == MACH_PORT_NULL)
    diskfs_exec = file_name_lookup (_SERVERS_EXEC, 0, 0);
  if (diskfs_exec == MACH_PORT_NULL)
    return EOPNOTSUPP;
  
  np = cred->po->np;
  if ((cred->po->openstat & O_EXEC) == 0)
    return EBADF;
  if (!((np->dn_stat.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH))
	|| ((np->dn_stat.st_mode & S_IUSEUNK)
	    && (np->dn_stat.st_mode & (S_IEXEC << S_IUNKSHIFT)))))
    return EACCES;

  /* Handle S_ISUID and S_ISGID uid substitution.  */
  /* XXX All this complexity should be moved to libfshelp.  -mib */
  if ((((np->dn_stat.st_mode & S_ISUID)
	&& !diskfs_isuid (np->dn_stat.st_uid, cred))
       || ((np->dn_stat.st_mode & S_ISGID)
	   && !diskfs_groupmember (np->dn_stat.st_gid, cred)))
      && !diskfs_isuid (0, cred))
    {
      /* XXX The test above was correct for the code before Roland
	 changed it; but now it's wrong.  This test decides when
	 permission is increasing, and therefore we need to 
	 protect the exec with NEWTASK and SECURE.  If permission
	 isn't increasing, then we still substitute id's, but we
	 don't to the SECURE or NEWTASK protection.  -mib */

      /* XXX Perhaps if there are errors in reauthenticating,
	 we should just run non-setuid?   */

      mach_port_t newauth, intermediate;
      void reauth (mach_port_t *port, int procp)
	{
	  mach_port_t newport, ref;
	  if (*port == MACH_PORT_NULL)
	    return;
	  ref = mach_reply_port ();
	  err = (procp ? proc_reauthenticate : io_reauthenticate)
	    (*port, ref, MACH_MSG_TYPE_MAKE_SEND);
	  if (! err)
	    err = auth_user_authenticate (newauth, *port,
					  ref, MACH_MSG_TYPE_MAKE_SEND,
					  &newport);
	  if (err)
	    {
	      /* Could not reauthenticate.  Do not give away the old port.  */
	      mach_port_deallocate (mach_task_self (), *port);
	      *port = MACH_PORT_NULL; /* XXX ? */
	    }
	  else if (newport != MACH_PORT_NULL)
	    {
	      mach_port_deallocate (mach_task_self (), *port);
	      *port = newport;
	    }
	  mach_port_destroy (mach_task_self (), ref);
	}

      uid_t auxuidbuf[2], genuidbuf[10];
      uid_t *old_aux_uids = auxuidbuf, *old_gen_uids = genuidbuf;
      int nold_aux_uids = 2, nold_gen_uids = 10;
      gid_t auxgidbuf[2], gengidbuf[10];
      gid_t *old_aux_gids = auxgidbuf, *old_gen_gids = gengidbuf;
      int nold_aux_gids = 2, nold_gen_gids = 10;
      int ngen_uids = nold_gen_uids ?: 1;
      int naux_uids = nold_aux_uids < 2 ? nold_aux_uids : 2;
      uid_t gen_uids[ngen_uids], aux_uids[naux_uids];
      int ngen_gids = nold_gen_gids ?: 1;
      int naux_gids = nold_aux_gids < 2 ? nold_aux_gids : 2;
      gid_t gen_gids[ngen_gids], aux_gids[naux_gids];
      
      int i;

      /* Tell the exec server to use secure ports and a new task.  */
      flags |= EXEC_SECURE|EXEC_NEWTASK;

      /* Find the IDs of the old auth handle.  */
      err = auth_getids (portarray[INIT_PORT_AUTH],
			 &old_gen_uids, &nold_aux_uids,
			 &old_aux_uids, &nold_aux_uids,
			 &old_gen_gids, &nold_gen_gids,
			 &old_aux_gids, &nold_aux_gids);
      if (err == MACH_SEND_INVALID_DEST)
	nold_gen_uids = nold_aux_uids = nold_gen_gids = nold_aux_gids = 0;
      else if (err)
	return err;

      /* XXX This is broken; there is no magical "nonexistent ID"
	 number.  The Posix numbering only matters for the exec of a
	 Posix process; this case can't be a problem, therefore.  Just
	 stuff the ID in slot 0 and nothing in slot 1.  */

      /* Set up the UIDs for the new auth handle.  */
      if (nold_aux_uids == 0)
	/* No real UID; we must invent one.  */
	aux_uids[0] = nold_gen_uids ? old_gen_uids[0] : -2; /* XXX */
      else
	{
	  aux_uids[0] = old_aux_uids[0];
	  if (nold_aux_uids > 2)
	    memcpy (&aux_uids[2], &old_aux_uids[2],
		    nold_aux_uids * sizeof (uid_t));
	}
      aux_uids[1] = old_gen_uids[0]; /* Set saved set-UID to effective UID.  */
      gen_uids[0] = np->dn_stat.st_uid;	/* Change effective to file owner.  */
      memcpy (&gen_uids[1], &old_gen_uids[1],
	      ((nold_gen_uids ?: 1) - 1) * sizeof (uid_t));

      /* Set up the GIDs for the new auth handle.  */
      if (nold_aux_gids == 0)
	/* No real GID; we must invent one.  */
	old_aux_gids[0] = nold_gen_gids ? old_gen_gids[0] : -2; /* XXX */
      else
	{
	  aux_gids[0] = old_aux_gids[0];
	  if (nold_aux_gids > 2)
	    memcpy (&aux_gids[2], &old_aux_gids[2],
		    nold_aux_gids * sizeof (gid_t));
	}
      aux_gids[1] = old_gen_gids[0]; /* Set saved set-GID to effective GID.  */
      gen_gids[0] = np->dn_stat.st_gid;	/* Change effective to file owner.  */
      memcpy (&gen_gids[1], &old_gen_gids[1],
	      ((nold_gen_gids ?: 1) - 1) * sizeof (gid_t));

      /* XXX This is totally wrong.  Just do one call to auth_makeauth
	 with both handles.  INTERMEDIATE here has no id's at all, and
	 so the second auth_makeauth call is guaranteed to fail.

	 It should give the user as close to the correct privilege as
	 possible as well; this requires looking inside the uid sets
	 and doing the "right thing".  If we are entirely unable to
	 increase the task's privilege, then abandon the setuid part,
	 but don't return an error.

	 -mib */

      /* Create the new auth handle.  First we must make a handle that
	 combines our IDs with those in the original user handle in
	 portarray[INIT_PORT_AUTH].  Only using that handle will we be
	 allowed to create the final handle, which contains secondary IDs
	 from the original user handle that we don't necessarily have.  */
      {
	mach_port_t handles[2] =
	  { diskfs_auth_server_port, portarray[INIT_PORT_AUTH] };
	err = auth_makeauth (diskfs_auth_server_port,
			     handles, MACH_MSG_TYPE_COPY_SEND, 2,
			     NULL, 0, NULL, 0, NULL, 0, NULL, 0,
			     &intermediate);
      }
      if (err)
	return err;
      err = auth_makeauth (intermediate,
			   NULL, MACH_MSG_TYPE_COPY_SEND, 0,
			   gen_uids, ngen_uids,
			   aux_uids, naux_uids,
			   gen_gids, ngen_gids,
			   aux_gids, naux_gids,
			   &newauth);
      mach_port_deallocate (mach_task_self (), intermediate);
      if (err)
	return err;

      /* Now we must reauthenticate all the ports to other
	 servers we pass along to it.  */
      
      for (i = 0; i < fdslen; ++i)
	reauth (&fds[i], 0);

      /* XXX The first two are unimportant; EXEC_SECURE is going to
	 blow them away anyhow.  -mib */
      reauth (&portarray[INIT_PORT_PROC], 1);
      reauth (&portarray[INIT_PORT_CRDIR], 0);
      reauth (&portarray[INIT_PORT_CWDIR], 0);

      mach_port_deallocate (mach_task_self (), portarray[INIT_PORT_AUTH]);
      portarray[INIT_PORT_AUTH] = newauth;
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

  err = exec_exec (diskfs_exec, 
		   (ports_get_right 
		    (diskfs_make_protid
		     (diskfs_make_peropen (np, O_READ, cred->po->dotdotport),
		      cred->uids, cred->nuids,
		      cred->gids, cred->ngids))),
		   MACH_MSG_TYPE_MAKE_SEND,
		   task, flags, argv, argvlen, envp, envplen, 
		   fds, MACH_MSG_TYPE_MOVE_SEND, fdslen,
		   portarray, MACH_MSG_TYPE_MOVE_SEND, portarraylen,
		   intarray, intarraylen, deallocnames, deallocnameslen,
		   destroynames, destroynameslen);
  mach_port_deallocate (mach_task_self (), task);
  return err;
}
