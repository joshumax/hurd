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
#include <string.h>

static int
scan_ids (uid_t *set, int setlen, uid_t test)
{
  int i;
  for (i = 0; i < setlen; i++)
    if (set[i] == test)
      return 1;
  return 0;
}

/* Adds ID to the sets OLDGENIDS & OLDAUXIDS, and returns the new set in
   GENIDS and AUXIDS, which are malloced.  SECURE is also updated to reflect
   whether a secure exec is called for.  ENOMEM is returned if a malloc
   fails, otherwise 0.  */
static error_t
setid (uid_t id, int *secure,
       uid_t *oldgenids, size_t noldgenids,
       uid_t *oldauxids, size_t noldauxids,
       uid_t **genids, size_t *ngenids,
       uid_t **auxids, size_t *nauxids)
{
  /* We are dumping the current first id; put it
     into the auxids array.  This is complex.  The different
     cases below are intended to make sure that we don't
     lose any ids (unlike posix) and to make sure that aux ids
     zero and one (if already set) behave like the posix
     ones. */

#define MALLOC(n) ({ void *_p = malloc(n); if (! _p) return ENOMEM; _p; })

  if (noldauxids == 0)
    {
      *auxids = MALLOC (*nauxids = 1);
      (*auxids)[0] = oldgenids[0];
    }
  else if (noldauxids == 1)
    {
      *auxids = MALLOC (*nauxids = 2);
      (*auxids)[0] = oldauxids[0];
      (*auxids)[1] = oldgenids[0];
    }
  else if (noldauxids == 2)
    {
      if (oldgenids[0] == oldauxids[1])
	{
	  *auxids = MALLOC (*nauxids = 2);
	  (*auxids)[0] = oldauxids[0];
	  (*auxids)[1] = oldauxids[1];
	}
      else
	{
	  /* Shift by one */
	  *auxids = MALLOC (*nauxids = 3);
	  (*auxids)[0] = oldauxids[0];
	  (*auxids)[1] = oldgenids[0];
	  (*auxids)[2] = oldauxids[1];
	}
    }
  else
    {
      /* Just like above, but in the shift case note
	 that the new (*auxids)[2] shouldn't be allowed
	 to needlessly duplicate something further on. */
      if (oldgenids[0] == oldauxids[1]
	  || scan_ids (&oldauxids[2], *nauxids - 2, oldauxids[1]))
	{
	  *auxids = MALLOC (*nauxids = noldauxids);
	  bcopy (oldauxids, *auxids, *nauxids);
	  (*auxids)[1] = oldgenids[0];
	}
      else 
	{
	  *auxids = MALLOC (*nauxids = noldauxids + 1);
	  (*auxids)[0] = oldauxids[0];
	  (*auxids)[1] = oldgenids[0];
	  bcopy (&oldauxids[1], &(*auxids)[2], noldauxids - 1);
	}
    }

  /* Whew.  Now set the new id. */
  *ngenids = noldgenids ?: 1;
  *genids = malloc (*ngenids);

  if (! *genids)
    {
      free (*auxids);
      return ENOMEM;
    }

  (*genids)[0] = id;
  if (noldgenids > 0)
    bcopy (&oldgenids[1], &(*genids)[1], *ngenids - 1);

  if (secure && !*secure
      && !scan_ids (oldgenids, noldgenids, id)
      && !scan_ids (oldauxids, noldauxids, id))
    *secure = 1;

  return 0;
}

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

      int i;
      
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
      if (suid)
	err = setid (np->dn_stat.st_uid, &secure,
		     oldgenuids, noldauxuids, oldauxuids, noldauxuids,
		     &genuids, &ngenuids, &auxuids, &nauxuids);
      if (sgid && !err)
	err = setid (np->dn_stat.st_gid, &secure,
		     oldgengids, noldauxgids, oldauxgids, noldauxgids,
		     &gengids, &ngengids, &auxgids, &nauxgids);

      if (scan_ids (oldgenuids, noldgenuids, 0) 
	  || scan_ids (oldauxuids, noldauxuids, 0))
	secure = 0;		/* If we're root, we don't have to be. */
      
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

      if (err)
	goto abandon_suid;	/* setid() failed.  */


      /* STEP 3: Attempt to create this new auth handle. */
      err = auth_makeauth (diskfs_auth_server_port, &portarray[INIT_PORT_AUTH],
			   MACH_MSG_TYPE_COPY_SEND, 1, 
			   genuids, ngenuids,
			   auxuids, nauxuids,
			   gengids, ngengids,
			   auxgids, nauxgids,
			   &newauth);
      if (err == EINVAL)
	/* The user's auth port was bogus.  As we can't trust what the user
	   has told us about ids, we use the authentication on the file being
	   execed (which we know is good), as the effective ids, and assume
	   no aux ids.  */
	{
	  /* Free our first attempts.  */
	  free (genuids);
	  free (auxuids);
	  free (gengids);
	  free (auxgids);

	  if (suid)
	    err = setid (np->dn_stat.st_uid, &secure,
			 cred->uids, cred->nuids, 0, 0,
			 &genuids, &ngenuids, &auxuids, &nauxuids);
	  if (sgid && !err)
	    err = setid (np->dn_stat.st_gid, &secure,
			 cred->gids, cred->ngids, 0, 0,
			 &gengids, &ngengids, &auxgids, &nauxgids);

	  if (diskfs_isuid (0, cred))
	    secure = 0;		/* If we're root, we don't have to be. */

	  if (err)
	    goto abandon_suid;	/* setid() failed.  */

	  /* Trrrry again...  */
	  err = auth_makeauth (diskfs_auth_server_port, 0,
			       MACH_MSG_TYPE_COPY_SEND, 1, 
			       genuids, ngenuids, auxuids, nauxuids,
			       gengids, ngengids, auxgids, nauxgids,
			       &newauth);
	}

      if (err)
	goto free_abandon_suid;
      
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

    free_abandon_suid:
      free (genuids);
      free (auxuids);
      free (gengids);
      free (auxgids);
    }
 abandon_suid:

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
