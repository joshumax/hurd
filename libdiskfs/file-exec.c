/*
   Copyright (C) 1993, 1994, 1995 Free Software Foundation

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
  struct protid *newpi;
  int suid, sgid, secure;
  
  if (!cred)
    return EOPNOTSUPP;

  if (diskfs_exec == MACH_PORT_NULL)
    diskfs_exec = file_name_lookup (_SERVERS_EXEC, 0, 0);
  if (diskfs_exec == MACH_PORT_NULL)
    return EOPNOTSUPP;
  
  np = cred->po->np;

  mutex_lock (&np->lock);

  if ((cred->po->openstat & O_EXEC) == 0)
    {
      mutex_unlock (&np->lock);
      return EBADF;
    }
  
  if (!((np->dn_stat.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH))
	|| ((np->dn_stat.st_mode & S_IUSEUNK)
	    && (np->dn_stat.st_mode & (S_IEXEC << S_IUNKSHIFT)))))
    {
      mutex_unlock (&np->lock);
      return EACCES;
    }
  
  if ((np->dn_stat.st_mode & S_IFMT) == S_IFDIR)
    {
      mutex_unlock (&np->lock);
      return EACCES;
    }

#ifdef this_is_right_but_not_quite_complete
  suid = np->dn_stat.st_mode & S_ISUID; /* XXX not if we can't do it... */
  sgid = np->dn_stat.st_mode & S_ISGID;	/* XXX not of we can't do it... */
  secure = 0;
  
  if (suid || sgid)
    {
      /* These variables describe the auth port that the
	 user gave us. */
      uid_t aubuf[10], gubuf[10], agbuf[20], ggbuf[20];
      uid_t *oldauxuids = aubuf, *oldgenuids = gubuf;
      gid_t *oldauxgids = agbuf, *oldgengids = ggbuf;
      int noldauxuids = 10, noldgenuids = 10;
      int noldauxgids = 20, noldgengids = 20;

      /* These describe the auth port we are trying to create. */
      uid_t *auxuids, *genuids;
      gid_t *auxgids, *gengids;
      int nauxuids, ngenuids;
      int nauxgids, ngengids;
      auth_t newauth;

      int isroot, i;

      int
	scan_ids (uid_t *set, int setlen, uid_t test)
	  {
	    int i;
	    for (i = 0; i < setlen; i++)
	      if (set[i] == test)
		return 1;
	    return 0;
	  }
      
      void
	reauth (mach_port_t *port, int isproc)
	  {
	    mach_port_t newport, ref;
	    if (*port == MACH_PORT_NULL)
	      return;
	    ref = mach_reply_port ();
	    err = (isproc ? proc_reauthenticate : io_reauthenticate)
	      (*port, ref, MACH_MSG_TYPE_MAKE_SEND);
	    if (!err)
	      err = auth_user_authenticate (newauth, *port, ref,
					    MACH_MSG_TYPE_MAKE_SEND, &newport);
	    if (err)
	      {
		/* Could not reauthenticate.  Roland thinks we should not
		   give away the old port.  I disagree; it can't actually hurt
		   because the old id's are still available, so it's no
		   security problem. */

		/* Nothing Happens. */
	      }
	    else
	      {
		if (isproc)
		  mach_port_deallocate (mach_task_self (), newport);
		else
		  {
		    mach_port_deallocate (mach_task_self (), *port);
		    *port = newport;
		  }
	      }
	    mach_port_destroy (mach_task_self (), ref);
	  }


      /* STEP 0: Fetch the user's current id's. */
      
      /* First fetch the current ID's the user has. */
      err = auth_getids (portarray[INIT_PORT_AUTH],
			 &oldgenuids, &noldgenuids,
			 &oldauxuids, &noldauxuids,
			 &oldgengids, &noldgengids,
			 &oldauxgids, &noldauxgids);
      if (err)
	goto abandon_suid;


      /* STEP 1: Find out if the user's permission will be
	 increasing, or just rearranged. */

      /* If the user's auth port is fraudulent, then these values
	 will be wrong.  No matter; we will repeat these checks
	 using secure id sets later if the port turns out to be 
	 bogus.  */
      isroot = (scan_ids (oldgenuids, noldgenuids, 0) 
		&& scan_ids (oldauxuids, noldauxuids, 0));
      if (!secure && suid && !isroot
	  && !scan_ids (oldgenuids, noldgenuids, np->dn_stat.st_uid)
	  && !scan_ids (oldauxuids, noldauxuids, np->dn_stat.st_uid))
	secure = 1;
      if (!secure && sgid && !isroot
	  && !scan_ids (oldgengids, noldgengids, np->dn_stat.st_gid)
	  && !scan_ids (oldauxgids, noldauxgids, np->dn_stat.st_gid))
	secure = 1;


      /* STEP 2: Rearrange the ids to decide what the new 
	 lists will look like. */
      
      if (suid)
	{
	  /* We are dumping the current first uid; put it
	     into the auxuids array.  This is complex.  The different
	     cases below are intended to make sure that we don't
	     lose any uids (unlike posix) and to make sure that aux ids
	     zero and one (if already set) behave like the posix
	     ones. */
	     
	  if (noldauxuids == 0)
	    {
	      auxuids = alloca (nauxuids = 1);
	      auxuids[0] = oldgenuids[0];
	    }
	  else if (noldauxuids == 1)
	    {
	      auxuids = alloca (nauxuids = 2);
	      auxuids[0] = oldauxuids[0];
	      auxuids[1] = oldgenuids[0];
	    }
	  else if (noldauxuids == 2)
	    {
	      if (oldgenuids[0] == oldauxuids[1])
		{
		  auxuids = alloca (nauxuids = 2);
		  auxuids[0] = oldauxuids[0];
		  auxuids[1] = oldauxuids[1];
		}
	      else
		{
		  /* Shift by one */
		  auxuids = alloca (nauxuids = 3);
		  auxuids[0] = oldauxuids[0];
		  auxuids[1] = oldgenuids[0];
		  auxuids[2] = oldauxuids[1];
		}
	    }
	  else
	    {
	      /* Just like above, but in the shift case note
		 that the new auxuids[2] shouldn't be allowed
		 to needlessly duplicate something further on. */
	      if (oldgenuids[0] == oldauxuids[1]
		  || scan_uids (&oldauxuids[2], nauxuids - 2, oldauxuids[1]))
		{
		  auxuids = alloca (nauxuids = noldauxuids);
		  bcopy (oldauxuids, auxuids, nauxuids);
		  auxuids[1] = oldgenuids[0];
		}
	      else 
		{
		  auxuids = alloca (nauxuids = noldauxuids + 1);
		  auxuids[0] = oldauxuids[0];
		  auxuids[1] = oldgenuids[0];
		  bcopy (&oldauxuids[1], &auxuids[2], noldauxuids - 1);
		}
	    }
	  
	  /* Whew.  Now set the new uid. */
	  genuids = alloca (ngenuids = noldgenuids);
	  genuids[0] = np->dn_stat.st_uid;
	  bcopy (&oldgenuids[1], &genuids[1], ngenuids - 1);
	}
      
      /* And now the same thing for group ids, mutatis mutandis. */
      if (sgid)
	{
	  /* We are dumping the current first gid; put it
	     into the auxgids array.  This is complex.  The different
	     cases below are intended to make sure that we don't
	     lose any gids (unlike posix) and to make sure that aux ids
	     zero and one (if already set) behave like the posix
	     ones. */
	     
	  if (noldauxgids == 0)
	    {
	      auxgids = alloca (nauxgids = 1);
	      auxgids[0] = oldgengids[0];
	    }
	  else if (noldauxgids == 1)
	    {
	      auxgids = alloca (nauxgids = 2);
	      auxgids[0] = oldauxgids[0];
	      auxgids[1] = oldgengids[0];
	    }
	  else if (noldauxgids == 2)
	    {
	      if (oldgengids[0] == oldauxgids[1])
		{
		  auxgids = alloca (nauxgids = 2);
		  auxgids[0] = oldauxgids[0];
		  auxgids[1] = oldauxgids[1];
		}
	      else
		{
		  /* Shift by one */
		  auxgids = alloca (nauxgids = 3);
		  auxgids[0] = oldauxgids[0];
		  auxgids[1] = oldgengids[0];
		  auxgids[2] = oldauxgids[1];
		}
	    }
	  else
	    {
	      /* Just like above, but in the shift case note
		 that the new auxgids[2] shouldn't be allowed
		 to needlessly duplicate something further on. */
	      if (oldgengids[0] == oldauxgids[1]
		  || scan_gids (&oldauxgids[2], nauxgids - 2, oldauxgids[1]))
		{
		  auxgids = alloca (nauxgids = noldauxgids);
		  bcopy (oldauxgids, auxgids, nauxgids);
		  auxgids[1] = oldgengids[0];
		}
	      else 
		{
		  auxgids = alloca (nauxgids = noldauxgids + 1);
		  auxgids[0] = oldauxgids[0];
		  auxgids[1] = oldgengids[0];
		  bcopy (&oldauxgids[1], &auxgids[2], noldauxgids - 1);
		}
	    }
	  
	  /* Whew.  Now set the new gid. */
	  gengids = alloca (ngengids = noldgengids);
	  gengids[0] = np->dn_stat.st_gid;
	  bcopy (&oldgengids[1], &gengids[1], ngengids - 1);
	}
      
      /* Deallocate the buffers if MiG allocated them. */
      if (oldgenuids != gubuf)
	vm_deallocate (mach_task_self (), (vm_address_t) oldgenuids, 
		       noldgenuids * sizeof (uid_t));
      if (oldauxuids != aubuf)
	vm_deallocate (mach_task_self (), (vm_address_t) oldauxuids,
		       noldauxuids * sizeof (uid_t));
      if (oldgengids != ggbuf)
	vm_deallocate (mach_task_self (), (vm_address_t) oldgengids,
		       noldgengids * (sizeof (gid_t)));
      if (oldauxgids != agbuf)
	vm_deallocate (mach_task_self (), (vm_address_t) oldauxgids,
		       noldauxgids * (sizeof (gid_t)));


      /* STEP 3: Attempt to create this new auth handle. */
      err = auth_makeauth (diskfs_auth_server_port, &portarray[INIT_PORT_AUTH],
			   MACH_MSG_TYPE_COPY_SEND, 1, 
			   genuids, ngenuids,
			   auxuids, nauxuids,
			   gengids, ngengids,
			   auxgids, nauxgids,
			   &newauth);
      if (err == EINVAL)
	{
	  /* The user's auth port was bogus.  We have to repeat the
	     check in step 1 above, but this time use the id's that
	     we have verified on the incoming request port. */
	  isroot = diskfs_isuid (cred, 0);
	  secure = 0;
	  if (suid && !isroot && !diskfs_isuid (cred, np->dn_stat.st_uid))
	    secure = 1;
	  if (!secure && sgid && !isroot
	      && !diskfs_groupmember (cred, np->dn_stat.st_gid))
	    secure = 1;

	  /* XXX Bad bug---the id's here came from the user's bogus
	     port; we shouldn't just trust them */

	  /* And now again try and create the new auth port, this
	     time not using the user for help. */
	  err = auth_makeauth (diskfs_auth_server_port, 
			       0, MACH_MSG_TYPE_COPY_SEND, 0, 
			       genuids, ngenuids,
			       auxuids, nauxuids,
			       gengids, ngengids,
			       auxgids, nauxgids,
			       &newauth);
	}

      if (err)
	goto abandon_suid;
      
      
      /* STEP 4: Re-authenticate all the ports we are handing to the user
	 with this new port, and install the new auth port in portarray. */
      for (i = 0; i < fdslen; ++i)
	reauth (&fds[i], 0);
      if (secure)
	{
	  /* Not worth doing these; the exec server will be 
	     doing them again for us. */
	  portarray[INIT_PORT_PROC] = MACH_PORT_NULL;
	  portarray[INIT_PORT_CRDIR] = MACH_PORT_NULL;
	}
      else
	{
	  reauth (&portarray[INIT_PORT_PROC], 1);
	  reauth (&portarray[INIT_PORT_CRDIR], 0);
	}
      reauth (&portarray[INIT_PORT_CWDIR], 0);
      mach_port_deallocate (mach_task_self (), portarray[INIT_PORT_AUTH]);
      portarray[INIT_PORT_AUTH] = newauth;


      /* STEP 5: If we must be secure, then set the appropriate flags
	 to tell the exec server so. */
      if (secure)
	flags |= EXEC_SECURE | EXEC_NEWTASK;
    }
 abandon_suid:
#endif
    

#ifdef this_is_so_totally_wrong_and_is_replaced_with_the_above
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
      
      unsigned int i;

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
#endif 

  /* If the user can't read the file, then we should use a new task,
     which would be inaccessible to the user.  Actually, this doesn't
     work, because the proc server will still give out the task port
     to the user.  Too many things depend on that that it can't be
     changed.  So this vague attempt isn't even worth trying.  */
#if 0
  if (diskfs_access (np, S_IREAD, cred))
    flags |= EXEC_NEWTASK;
#endif

  newpi = diskfs_make_protid (diskfs_make_peropen (np, O_READ, 
						   cred->po->dotdotport),
			      cred->uids, cred->nuids,
			      cred->gids, cred->ngids);
  mutex_unlock (&np->lock);

  err = exec_exec (diskfs_exec, 
		   ports_get_right (newpi),
		   MACH_MSG_TYPE_MAKE_SEND,
		   task, flags, argv, argvlen, envp, envplen, 
		   fds, MACH_MSG_TYPE_COPY_SEND, fdslen,
		   portarray, MACH_MSG_TYPE_COPY_SEND, portarraylen,
		   intarray, intarraylen, deallocnames, deallocnameslen,
		   destroynames, destroynameslen);

  ports_port_deref (newpi);
  if (!err)
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
