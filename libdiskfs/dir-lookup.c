/* libdiskfs implementation of fs.defs:dir_lookup
   Copyright (C) 1992, 1993, 1994, 1995, 1996, 1997 Free Software Foundation

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

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <hurd/fsys.h>
#include <hurd/paths.h>

#include "priv.h"
#include "fs_S.h"

/* XXX - Temporary hack; this belongs in a header file, probably types.h. */
#define major(x) ((int)(((unsigned) (x) >> 8) & 0xff))
#define minor(x) ((int)((x) & 0xff))

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
  struct protid *newpi;
  unsigned depth;		/* Depth of DNP below FS root.  */

  if (!dircred)
    return EOPNOTSUPP;

  /* XXX - EXLOCK & SHLOCK are temporary until they get added to O_HURD. */
  flags &= O_HURD | O_EXLOCK | O_SHLOCK;  

  create = (flags & O_CREAT);
  excl = (flags & O_EXCL);

  /* Skip leading slashes */
  while (path[0] == '/')
    path++;

  *returned_port_poly = MACH_MSG_TYPE_MAKE_SEND;
  *retry = FS_RETRY_NORMAL;
  retryname[0] = '\0';

  /* How far beneath root we are; this changes as we traverse the path.  */
  depth = dircred->po->depth;

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
	  error = diskfs_lookup (dnp, path, CREATE, &np, ds, dircred, depth, &depth);
	}
      else
	error = diskfs_lookup (dnp, path, LOOKUP, &np, 0, dircred, depth, &depth);

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
	      if (diskfs_synchronous)
		{
		  diskfs_file_update (dnp, 1);
		  diskfs_file_update (np, 1);
		}
	      newnode = 1;
	    }
	  else
	    diskfs_drop_dirstat (dnp, ds);
	}
      
      if (error)
	goto out;

      /* If this is translated, start the translator (if necessary)
	 and return.  */
      if ((((flags & O_NOTRANS) == 0) || !lastcomp)
	  && ((np->dn_stat.st_mode & S_IPTRANS)
	      || S_ISFIFO (np->dn_stat.st_mode)
	      || S_ISCHR (np->dn_stat.st_mode)
	      || S_ISBLK (np->dn_stat.st_mode)
	      || fshelp_translated (&np->transbox)))
	{
	  mach_port_t dirport;
	  struct diskfs_trans_callback_cookie2 cookie2 =
	    { dircred->po->dotdotport, depth };
	  
	  /* A callback function for short-circuited translators.
	     Symlink & ifsock are handled elsewhere.  */
	  error_t short_circuited_callback1 (void *cookie1, void *cookie2,
					     uid_t *uid, gid_t *gid,
					     char **argz, int *argz_len)
	    {
	      struct node *node = cookie1;

	      switch (node->dn_stat.st_mode & S_IFMT)
		{
		case S_IFCHR:
		case S_IFBLK:
		  asprintf (argz, "%s%c%d%c%d",
			    (S_ISCHR (node->dn_stat.st_mode)
			     ? _HURD_CHRDEV : _HURD_BLKDEV),
			    0, major (node->dn_stat.st_rdev),
			    0, minor (node->dn_stat.st_rdev));
		  *argz_len = strlen (*argz) + 1;
		  *argz_len += strlen (*argz + *argz_len) + 1;
		  *argz_len += strlen (*argz + *argz_len) + 1;
		  break;
		case S_IFIFO:
		  asprintf (argz, "%s", _HURD_FIFO);
		  *argz_len = strlen (*argz) + 1;
		  break;
		default:
		  return ENOENT;
		}

	      *uid = node->dn_stat.st_uid;
	      *gid = node->dn_stat.st_gid;

	      return 0;
	    }

	  /* Create an unauthenticated port for DNP, and then
	     unlock it. */
	  error = 
	    diskfs_create_protid (diskfs_make_peropen (dnp, 0,
						       dircred->po->dotdotport,
						       depth),
				  iohelp_create_iouser (make_idvec (), 
							make_idvec ()),
				  &newpi);
	  if (error)
	    goto out;

	  dirport = ports_get_right (newpi);
	  mach_port_insert_right (mach_task_self (), dirport, dirport,
				  MACH_MSG_TYPE_MAKE_SEND);
	  ports_port_deref (newpi);
	  if (np != dnp)
	    mutex_unlock (&dnp->lock);

	  error = fshelp_fetch_root (&np->transbox, &cookie2,
				     dirport, dircred->user,
				     lastcomp ? flags : 0,
				     ((np->dn_stat.st_mode & S_IPTRANS)
				      ? _diskfs_translator_callback1
				      : short_circuited_callback1),
				     _diskfs_translator_callback2,
				     retry, retryname, returned_port);

	  /* fetch_root copies DIRPORT for success, so we always should
	     deallocate our send right.  */
	  mach_port_deallocate (mach_task_self (), dirport);

	  if (error != ENOENT)
	    {
	      diskfs_nrele (dnp);
	      diskfs_nput (np);
	      *returned_port_poly = MACH_MSG_TYPE_MOVE_SEND;
	      if (!lastcomp && !error)
		{
		  strcat (retryname, "/");
		  strcat (retryname, nextname);
		}
	      return error;
	    }

	  /* ENOENT means there was a hiccup, and the translator
	     vanished while NP was unlocked inside fshelp_fetch_root.
	     Reacquire the locks, and continue as normal. */
	  error = 0;
	  if (np != dnp)
	    {
	      if (!strcmp (path, ".."))
		mutex_lock (&dnp->lock);
	      else
		{
		  mutex_unlock (&np->lock);
		  mutex_lock (&dnp->lock);
		  mutex_lock (&np->lock);
		}
	    }
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
	error = fshelp_access (&np->dn_stat, S_IREAD, dircred->user);

      if (!error && (flags & O_EXEC))
	error = fshelp_access (&np->dn_stat, S_IEXEC, dircred->user);

      if (!error && (flags & O_WRITE))
	{
	  if (type == S_IFDIR)
	    error = EISDIR;
	  else if (diskfs_check_readonly ())
	    error = EROFS;
	  else
	    error = fshelp_access (&np->dn_stat, S_IWRITE, dircred->user);
	}

      if (error)
	goto out;
    }
    
  if ((flags & O_NOATIME) 
      && (fshelp_isowner (&np->dn_stat, dircred->user) == EPERM))
    flags &= ~O_NOATIME;
      
  error =
    diskfs_create_protid (diskfs_make_peropen (np, 
					       (flags &~OPENONLY_STATE_MODES), 
					       dircred->po->dotdotport,
					       depth),
			  dircred->user, &newpi);

  if (! error)
    {
      if (flags & O_EXLOCK)
	error = fshelp_acquire_lock (&np->userlock, &newpi->po->lock_status,
				     &np->lock, LOCK_EX);
      else if (flags & O_SHLOCK)
	error = fshelp_acquire_lock (&np->userlock, &newpi->po->lock_status,
				     &np->lock, LOCK_SH);
      if (error)
	ports_port_deref (newpi); /* Get rid of NEWPI.  */
    }
  
  if (! error)
    {
      *returned_port = ports_get_right (newpi);
      ports_port_deref (newpi);
    }
  
 out:
  if (np)
    if (dnp == np)
      diskfs_nrele (np);
    else
      diskfs_nput (np);
  if (dnp)
    diskfs_nput (dnp);

  return error;
}
