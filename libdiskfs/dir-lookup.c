/* libdiskfs implementation of fs.defs:dir_lookup
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

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
#include <fcntl.h>
#include <string.h>
#include <hurd/fsys.h>

/* Implement dir_lookup as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_dir_lookup (struct protid *dircred,
		     char *path,
		     int flags,
		     mode_t mode,
		     enum retry_type *retry,
		     char *retryname,
		     file_t *returned_port,
		     mach_msg_type_name_t *returned_port_poly)
{
  struct node *dnp;
  struct node *np;
  int nsymlink = 0;
  char *nextname;
  int nextnamelen;
  error_t error = 0;
  char *pathbuf = 0;
  int pathbuflen = 0;
  int newnamelen;
  int create, excl;
  int lastcomp = 0;
  int newnode = 0;
  struct dirstat *ds = 0;
  int mustbedir = 0;
  int amt;
  int type;

  if (!dircred)
    return EOPNOTSUPP;

  flags &= O_HURD;
  
  create = (flags & O_CREAT);
  excl = (flags & O_EXCL);

  /* Skip leading slashes */
  while (path[0] == '/')
    path++;

  *returned_port_poly = MACH_MSG_TYPE_MAKE_SEND;
  *retry = FS_RETRY_NORMAL;
  retryname[0] = '\0';

  if (path[0] == '\0')
    {
      mustbedir = 1;

      /* Set things up in the state expected by the code from gotit: on. */
      dnp = 0;
      np = dircred->po->np;
      mutex_lock (&np->lock);
      diskfs_nref (np);
      goto gotit;
    }

  dnp = dircred->po->np;
  mutex_lock (&dnp->lock);
  np = 0;

  diskfs_nref (dnp);		/* acquire a reference for later diskfs_nput */

  do
    {
      assert (!lastcomp);
      
      /* Find the name of the next pathname component */
      nextname = index (path, '/');

      if (nextname)
	{
	  *nextname++ = '\0';
	  while (*nextname == '/')
	    nextname++;
	  if (*nextname == '\0')
	    {
	      /* These are the rules for filenames ending in /. */
	      nextname = 0;
	      lastcomp = 1;
	      mustbedir = 1;
	      create = 0;
	    }
	  else
	    lastcomp = 0;
	}
      else
	lastcomp = 1;
	  
      np = 0;

      /* diskfs_lookup the next pathname component */
      if (lastcomp && create)
	{
	  assert (!ds);
	  ds = alloca (diskfs_dirstat_size);
	  error = diskfs_lookup (dnp, path, CREATE, &np, ds, dircred);
	}
      else
	error = diskfs_lookup (dnp, path, LOOKUP, &np, 0, dircred);

      if (lastcomp && create && excl && (!error || error == EAGAIN))
	error = EEXIST;

      /* If we get an error we're done */
      if (error == EAGAIN)
	{
	  if (dircred->po->dotdotport != MACH_PORT_NULL)
	    {
	      *retry = FS_RETRY_REAUTH;
	      *returned_port = dircred->po->dotdotport;
	      *returned_port_poly = MACH_MSG_TYPE_COPY_SEND;
	      if (!lastcomp)
		strcpy (retryname, nextname);
	      error = 0;
	      goto out;
	    }
	  else
	    {
	      error = 0;
	      np = dnp;
	      diskfs_nref (np);
	    }
	}

      /* Create the new node if necessary */
      if (lastcomp && create)
	{
	  if (error == ENOENT)
	    {
	      mode &= ~(S_IFMT | S_ISPARE | S_ISVTX);
	      mode |= S_IFREG;
	      error = diskfs_create_node (dnp, path, mode, &np, dircred, ds);
	      newnode = 1;
	    }
	  else
	    diskfs_drop_dirstat (dnp, ds);
	}
      
      if (error)
	goto out;

      /* If this is translated, start the translator (if necessary)
	 and return.  */
      /* The check for `np != dnp' simplifies this code a great
	 deal.  Such a translator should already have been started,
	 so there's no lossage in doing it this way. */
      if ((!lastcomp || !(flags & O_NOTRANS))
	  && np != dnp)
	{
	  mach_port_t control;
	  uid_t *uids = 0, *gids;
	  int nuids, ngids;
	  file_t dirfile = MACH_PORT_NULL;

	  /* Be very careful not to hold an inode lock while fetching
	     a translator lock and vice versa.  */

	  mutex_unlock (&np->lock);
	  mutex_unlock (&dnp->lock);

	repeat_trans:
	  mutex_lock (&np->translator.lock);
	  if (np->translator.control != MACH_PORT_NULL)
	    {
	      control = np->translator.control;
	      mach_port_mod_refs (mach_task_self (), control, 
				  MACH_PORT_RIGHT_SEND, 1);
	      mutex_unlock (&np->translator.lock);
	      
	      /* Now we have a copy of the translator port that isn't
		 dependent on the translator lock itself.  Relock
		 the directory, make a port from it, and then call
		 fsys_getroot. */

	      if (dirfile == MACH_PORT_NULL)
		{
		  mutex_lock (&dnp->lock);
		  dirfile = (ports_get_right
			     (diskfs_make_protid
			      (diskfs_make_peropen (dnp, 0, 
						    dircred->po->dotdotport),
			       0, 0, 0, 0)));
		  mach_port_insert_right (mach_task_self (), dirfile, dirfile,
					  MACH_MSG_TYPE_MAKE_SEND);
		  mutex_unlock (&dnp->lock);
		}

	      if (!uids)
		{
		  uids = dircred->uids;
		  gids = dircred->gids;
		  nuids = dircred->nuids;
		  ngids = dircred->ngids;
		}
	      
	      /* We turn off O_NOLINK here if this is not the last
		 component because fsys_getroot always thinks it's the
		 last node. */
	      error = fsys_getroot (control, dirfile, MACH_MSG_TYPE_COPY_SEND,
				    uids, nuids, gids, ngids,
				    lastcomp ? flags : flags & ~O_NOLINK,
				    retry, retryname, returned_port);
	      
	      /* If we got MACH_SEND_INVALID_DEST or MIG_SERVER_DIED, then
		 the server is dead.  Zero out the old control port and try
		 everything again.  */

	      if (error == MACH_SEND_INVALID_DEST || error == MIG_SERVER_DIED)
		{
		  mutex_lock (&np->translator.lock);

		  /* Only zero it if it hasn't changed. */

		  if (np->translator.control == control)
		    fshelp_translator_drop (&np->translator);
		  mutex_unlock (&np->translator.lock);
	      
		  /* And we're done with this port. */
		  mach_port_deallocate (mach_task_self (), control);

		  goto repeat_trans;
		}
	      
	      /* Otherwise, we're done; return to the user.  If there
		 are more components after this name, be sure to append
		 them to the user's retry path. */
	      if (!error && !lastcomp)
		{
		  strcat (retryname, "/");
		  strcat (retryname, nextname);
		}
	      *returned_port_poly = MACH_MSG_TYPE_MOVE_SEND;
	      diskfs_nrele (dnp);
	      diskfs_nrele (np);
	      mach_port_deallocate (mach_task_self (), dirfile);
	      return error;
	    }
	  else
	    {
	      /* If we get here, then we have no active control port.
		 Check to see if there is a passive translator, and if so
		 repeat the translator check. */
	      mutex_unlock (&np->translator.lock);
	      
	      mutex_lock (&np->lock);
	      if (diskfs_node_translated (np))
		{
		  /* Start the translator. */
		  if (dirfile == MACH_PORT_NULL)
		    {
		      /* This code is the same as above. */
		      mutex_unlock (&np->lock);
		      mutex_lock (&dnp->lock);
		      dirfile = (ports_get_right
				 (diskfs_make_protid
				  (diskfs_make_peropen (dnp, 0, 
							dircred->po
							->dotdotport),
				   0, 0, 0, 0)));
		      mach_port_insert_right (mach_task_self (), dirfile,
					      dirfile,
					      MACH_MSG_TYPE_MAKE_SEND);
		      mutex_unlock (&dnp->lock);
		      mutex_lock (&np->lock);
		    }
		  error = diskfs_start_translator (np, dirfile, dircred);
		  if (error)
		    {
		      mach_port_deallocate (mach_task_self (), dirfile);
		      diskfs_nrele (dnp);
		      diskfs_nput (np);
		      return error;
		    }
		  goto repeat_trans;
		}
	    }
	  
	  /* We're here if we tried the translator check, and it
	     failed.   Lock everything back, and make sure we do it
	     in the right order. */
	  if (strcmp (path, ".."))
	    {
	      mutex_unlock (&np->lock);
	      mutex_lock (&dnp->lock);
	      mutex_lock (&np->lock);
	    }
	  else
	    mutex_lock (&dnp->lock);
	}
      
      if (S_ISLNK (np->dn_stat.st_mode)
	  && !(lastcomp && (flags & (O_NOLINK|O_NOTRANS))))
	{
	  /* Handle symlink interpretation */

	  if (nsymlink++ > diskfs_maxsymlinks)
	    {
	      error = ELOOP;
	      goto out;
	    }
	      
	  nextnamelen = nextname ? strlen (nextname) + 1 : 0;
	  newnamelen = nextnamelen + np->dn_stat.st_size + 1;
	  if (pathbuflen < newnamelen)
	    {
	      pathbuf = alloca (newnamelen);
	      pathbuflen = newnamelen;
	    }
	      
	  if (diskfs_read_symlink_hook)
	    error = (*diskfs_read_symlink_hook)(np, pathbuf);
	  if (!diskfs_read_symlink_hook || error == EINVAL)
	    error = diskfs_node_rdwr (np, pathbuf, 
				      0, np->dn_stat.st_size, 0, 
				      dircred, &amt);
	  if (error)
	    goto out;
	      
	  if (nextname)
	    {
	      pathbuf[np->dn_stat.st_size] = '/';
	      bcopy (nextname, pathbuf + np->dn_stat.st_size + 1, 
		     nextnamelen - 1);
	    }
	  pathbuf[nextnamelen + np->dn_stat.st_size] = '\0';

	  if (pathbuf[0] == '/')
	    {
	      /* Punt to the caller.  */
	      *retry = FS_RETRY_MAGICAL;
	      *returned_port = MACH_PORT_NULL;
	      strcpy (retryname, pathbuf);
	      goto out;
	    }
	  
	  path = pathbuf;
	  if (lastcomp)
	    {
	      lastcomp = 0;
	      /* Symlinks to nonexistent files aren't allowed to cause
		 creation, so clear the flag here. */
	      create = 0;
	    }
	  diskfs_nput (np);
	  np = 0;
	}
      else
	{
	  /* Handle normal nodes */
	  path = nextname;
	  if (np == dnp)
	    diskfs_nrele (dnp);
	  else
	    diskfs_nput (dnp);
	  if (!lastcomp)
	    {
	      dnp = np;
	      np = 0;
	    }
	  else
	    dnp = 0;
	}
    } while (path && *path);
  
  /* At this point, np is the node to return.  If newnode is set, then
     we just created this node.  */

 gotit:
  type = np->dn_stat.st_mode & S_IFMT;

  if (mustbedir && type != S_IFDIR)
    {
      error = ENOTDIR;
      goto out;
    }
  
  if (!newnode)
    /* Check permissions on existing nodes, but not new ones. */
    {
      if ((type == S_IFSOCK || type == S_IFBLK || type == S_IFLNK
	   || type == S_IFCHR || type == S_IFIFO)
	  && (flags & (O_READ|O_WRITE|O_EXEC)))
	error = EOPNOTSUPP;

      if (!error && (flags & O_READ))
	error = diskfs_access (np, S_IREAD, dircred);

      if (!error && (flags & O_EXEC))
	error = diskfs_access (np, S_IEXEC, dircred);

      if (!error && (flags & O_WRITE))
	{
	  if (type == S_IFDIR)
	    error = EISDIR;
	  else if (diskfs_readonly)
	    error = EROFS;
	  else
	    error = diskfs_access (np, S_IWRITE, dircred);
	}

      if (error)
	goto out;
    }
    
  if ((flags & O_NOATIME) && (diskfs_isowner (np, dircred) == EPERM))
    flags &= ~O_NOATIME;

  flags &= ~OPENONLY_STATE_MODES;
      
  *returned_port = (ports_get_right
		    (diskfs_make_protid 
		     (diskfs_make_peropen (np, flags, dircred->po->dotdotport),
		      dircred->uids, dircred->nuids,
		      dircred->gids, dircred->ngids)));

  
 out:
  if (np)
    {
      if (dnp == np)
	diskfs_nrele (np);
      else
	diskfs_nput (np);
    }
  if (dnp)
    diskfs_nput (dnp);
  return error;
}
