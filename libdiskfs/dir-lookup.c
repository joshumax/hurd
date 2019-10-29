/* libdiskfs implementation of fs.defs:dir_lookup

   Copyright (C) 1992-2002, 2008, 2013-2019
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <hurd/fshelp.h>
#include <hurd/fsys.h>
#include <hurd/paths.h>

#include "priv.h"
#include "fs_S.h"

/* Implement dir_lookup as described in <hurd/fs.defs>. */
error_t
diskfs_S_dir_lookup (struct protid *dircred,
		     char *filename,
		     int flags,
		     mode_t mode,
		     retry_type *do_retry,
		     char *retry_name,
		     mach_port_t *retry_port,
		     mach_msg_type_name_t *retry_port_type)
{
  struct node *dnp;
  struct node *np;
  int nsymlinks = 0;
  char *nextname;
  char *relpath;
  int nextnamelen;
  error_t err = 0;
  char *pathbuf = 0;
  int pathbuflen = 0;
  int newnamelen;
  int create, excl;
  int lastcomp = 0;
  int newnode = 0;
  struct dirstat *ds = 0;
  int mustbedir = 0;
  size_t amt;
  int type;
  struct protid *newpi = 0;
  struct peropen *newpo = 0;

  if (!dircred)
    return EOPNOTSUPP;

  flags &= O_HURD;

  create = (flags & O_CREAT);
  excl = (flags & O_EXCL);

  /* Skip leading slashes */
  while (*filename == '/')
    filename++;

  /* Preserve the path relative to dircred->po->path.  */
  relpath = strdup (filename);
  if (! relpath)
    return ENOMEM;

  /* Keep a pointer to the start of the filename for length
     calculations.  */
  char *filename_start = filename;

  *retry_port_type = MACH_MSG_TYPE_MAKE_SEND;
  *do_retry = FS_RETRY_NORMAL;
  *retry_name = '\0';

  if (*filename == '\0')
    {
      /* Set things up in the state expected by the code from gotit: on. */
      dnp = 0;
      np = dircred->po->np;
      pthread_mutex_lock (&np->lock);
      diskfs_nref (np);
      goto gotit;
    }

  dnp = dircred->po->np;
  pthread_mutex_lock (&dnp->lock);

  diskfs_nref (dnp);		/* acquire a reference for later diskfs_nput */

  do
    {
      assert_backtrace (!lastcomp);

      /* Find the name of the next pathname component */
      nextname = index (filename, '/');

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
	  if (!ds)
	    ds = alloca (diskfs_dirstat_size);
	  err = diskfs_lookup (dnp, filename, CREATE, &np, ds, dircred);
	}
      else
	err = diskfs_lookup (dnp, filename, LOOKUP, &np, 0, dircred);

      if (lastcomp && create && excl && (!err || err == EAGAIN))
	err = EEXIST;

      /* If we get an error we're done */
      if (err == EAGAIN)
	{
	  if (dnp == dircred->po->shadow_root)
	    /* We're at the root of a shadow tree.  */
	    {
	      if (dircred->po->shadow_root_parent == MACH_PORT_NULL)
		{
		  /* This is a shadow root with no parent, meaning
		     we should treat it as a virtual root disconnected
		  from its real .. directory.  */
		  err = 0;
		  np = dnp;
		  diskfs_nref (np);
		}
	      else
		{
		  /* Punt the client up to the shadow root parent.  */
		  *do_retry = FS_RETRY_REAUTH;
		  *retry_port = dircred->po->shadow_root_parent;
		  *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
		  if (lastcomp && mustbedir) /* Trailing slash.  */
		    strcpy (retry_name, "/");
		  else if (!lastcomp)
		    strcpy (retry_name, nextname);
		  err = 0;
		  goto out;
		}
	    }
	  else if (dircred->po->root_parent != MACH_PORT_NULL)
	    /* We're at a real translator root; even if DIRCRED->po has a
	       shadow root, we can get here if its in a directory that was
	    renamed out from under it...  */
	    {
	      *do_retry = FS_RETRY_REAUTH;
	      *retry_port = dircred->po->root_parent;
	      *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
	      if (lastcomp && mustbedir) /* Trailing slash.  */
		strcpy (retry_name, "/");
	      else if (!lastcomp)
		strcpy (retry_name, nextname);
	      err = 0;
	      goto out;
	    }
	  else
	    /* We're at a REAL root, as in there's no way up from here.  */
	    {
	      err = 0;
	      np = dnp;
	      diskfs_nref (np);
	    }
	}

      /* Create the new node if necessary */
      if (lastcomp && create)
	{
	  if (err == ENOENT)
	    {
	      mode &= ~(S_IFMT | S_ISPARE | S_ISVTX | S_ITRANS);
	      mode |= S_IFREG;
	      err = diskfs_create_node (dnp, filename, mode, &np, dircred, ds);
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

      if (err)
	goto out;

      /* If this is translated, start the translator (if necessary)
	 and return.  */
      if ((((flags & O_NOTRANS) == 0) || !lastcomp || mustbedir)
	  && ((np->dn_stat.st_mode & S_IPTRANS)
	      || S_ISFIFO (np->dn_stat.st_mode)
	      || S_ISCHR (np->dn_stat.st_mode)
	      || S_ISBLK (np->dn_stat.st_mode)
	      || fshelp_translated (&np->transbox)))
	{
	  mach_port_t dirport;
	  struct iouser *user;

	  /* Create an unauthenticated port for DNP, and then
	     unlock it. */
	  err = iohelp_create_empty_iouser (&user);
	  if (! err)
	    {
	      err = diskfs_make_peropen (dnp, 0, dircred->po, &newpo);
	      if (! err)
		{
		  err = diskfs_create_protid (newpo, user, &newpi);
		  if (! err)
		    newpo = 0;
		}

	      iohelp_free_iouser (user);
	    }

	  if (err)
	    goto out;

	  dirport = ports_get_send_right (newpi);
	  if (np != dnp)
	    pthread_mutex_unlock (&dnp->lock);

	  struct fshelp_stat_cookie2 cookie = {
	    .statp = &np->dn_stat,
	    .modep = &np->dn_stat.st_mode,
	    .next = dircred->po,
	  };
	  err = fshelp_fetch_root (&np->transbox,
				   &cookie,
				   dirport,
				   dircred->user,
				   lastcomp ? flags : 0,
				   ((np->dn_stat.st_mode & S_IPTRANS)
				    ? _diskfs_translator_callback1
				    : fshelp_short_circuited_callback1),
				   _diskfs_translator_callback2,
				   do_retry, retry_name, retry_port);

	  /* fetch_root copies DIRPORT for success, so we always should
	     deallocate our send right.  */
	  mach_port_deallocate (mach_task_self (), dirport);

	  if (err != ENOENT)
	    {
	      *retry_port_type = MACH_MSG_TYPE_MOVE_SEND;
	      if (!err)
		{
		  char *end = strchr (retry_name, '\0');
		  char *translator_path = strdupa (relpath);
		  char *complete_path;

		  if (mustbedir)
		    *end++ = '/'; /* Trailing slash.  */
		  else if (!lastcomp) {
		    if (end != retry_name)
		      *end++ = '/';
		    strcpy (end, nextname);
		  }

		  if (nextname != NULL)
		    {
		      /* This was not the last path component.
			 NEXTNAME points to the next component, locate
			 the end of the current component and use it
			 to trim TRANSLATOR_PATH.  */
		      end = nextname;
		      while (*end != 0)
			end--;
		      translator_path[end - filename_start] = '\0';
		    }

		  /* Trim trailing slashes.  */
		  end = &translator_path[strlen (translator_path) - 1];
		  while (*end == '/' && end >= translator_path)
		    *end = '\0', end--;

		  if (dircred->po->path == NULL
		      || !strcmp (dircred->po->path,"."))
		    /* dircred is the root directory.  */
		    complete_path = translator_path;
		  else
		    asprintf (&complete_path, "%s/%s", dircred->po->path,
			      translator_path);

		  err = fshelp_set_active_translator (&newpi->pi,
						      complete_path,
						      &np->transbox);
		  if (complete_path != translator_path)
		    free(complete_path);
		  if (err)
		    goto out;
		}

	      goto out;
	    }

	  ports_port_deref (newpi);
	  newpi = NULL;

	  /* ENOENT means there was a hiccup, and the translator
	     vanished while NP was unlocked inside fshelp_fetch_root.
	     Reacquire the locks, and continue as normal. */
	  err = 0;
	  if (np != dnp)
	    {
	      if (!strcmp (filename, ".."))
		pthread_mutex_lock (&dnp->lock);
	      else
		{
		  if (pthread_mutex_trylock (&dnp->lock))
		    {
		      pthread_mutex_unlock (&np->lock);
		      pthread_mutex_lock (&dnp->lock);
		      pthread_mutex_lock (&np->lock);
		    }
		}
	    }
	}

      if (S_ISLNK (np->dn_stat.st_mode)
	  && (!lastcomp
	      || mustbedir	/* "foo/" must see that foo points to a dir */
	      || !(flags & (O_NOLINK|O_NOTRANS))))
	{
	  /* Handle symlink interpretation */

	  if (nsymlinks++ > diskfs_maxsymlinks)
	    {
	      err = ELOOP;
	      goto out;
	    }

	  nextnamelen = nextname ? strlen (nextname) + 1 : 0;
	  newnamelen = nextnamelen + np->dn_stat.st_size + 1 + 1;
	  if (pathbuflen < newnamelen)
	    {
	      pathbuf = alloca (newnamelen);
	      pathbuflen = newnamelen;
	    }

	  if (diskfs_read_symlink_hook)
	    err = (*diskfs_read_symlink_hook)(np, pathbuf);
	  if (!diskfs_read_symlink_hook || err == EINVAL)
	    {
	      err = diskfs_node_rdwr (np, pathbuf,
					0, np->dn_stat.st_size, 0,
					dircred, &amt);
	      if (!err)
		assert_backtrace (amt == np->dn_stat.st_size);
	    }
	  if (err)
	    goto out;

	  if (np->dn_stat.st_size == 0)	/* symlink to "" */
	    filename = nextname;
	  else
	    {
	      if (nextname)
		{
		  pathbuf[np->dn_stat.st_size] = '/';
		  memcpy (pathbuf + np->dn_stat.st_size + 1,
			  nextname, nextnamelen - 1);
		}
	      if (mustbedir)
		{
		  pathbuf[nextnamelen + np->dn_stat.st_size] = '/';
		  pathbuf[nextnamelen + np->dn_stat.st_size + 1] = '\0';
		}
	      else
		pathbuf[nextnamelen + np->dn_stat.st_size] = '\0';

	      if (pathbuf[0] == '/')
		{
		  /* Punt to the caller.  */
		  *do_retry = FS_RETRY_MAGICAL;
		  *retry_port = MACH_PORT_NULL;
		  strcpy (retry_name, pathbuf);
		  goto out;
		}

	      filename = pathbuf;
	      mustbedir = 0;
	    }

	  if (lastcomp)
	    lastcomp = 0;

	  diskfs_nput (np);
	  np = 0;

	  if (filename == 0)	/* symlink to "" was the last component */
	    {
	      np = dnp;
	      dnp = 0;
	      break;
	    }
	}
      else
	{
	  /* Handle normal nodes */
	  filename = nextname;
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
    }
  while (filename && *filename);

  /* At this point, np is the node to return.  If newnode is set, then
     we just created this node.  */

 gotit:
  type = np->dn_stat.st_mode & S_IFMT;

  if (mustbedir && type != S_IFDIR)
    {
      err = ENOTDIR;
      goto out;
    }

  if (!newnode)
    /* Check permissions on existing nodes, but not new ones. */
    {
      if ((type == S_IFSOCK || type == S_IFBLK || type == S_IFCHR ||
	    type == S_IFIFO)
	   && (flags & (O_READ|O_WRITE|O_EXEC)))
	err = EACCES;

      if (!err && type == S_IFLNK && (flags & (O_WRITE|O_EXEC)))
	err = ELOOP;

      if (!err && (flags & O_READ))
	err = fshelp_access (&np->dn_stat, S_IREAD, dircred->user);

      if (!err && (flags & O_EXEC))
	err = fshelp_access (&np->dn_stat, S_IEXEC, dircred->user);

      if (!err && (flags & O_WRITE))
	{
	  if (type == S_IFDIR)
	    err = EISDIR;
	  else if (diskfs_check_readonly ())
	    err = EROFS;
	  else
	    err = fshelp_access (&np->dn_stat, S_IWRITE, dircred->user);
	}

      if (err)
	goto out;
    }

  if ((flags & O_NOATIME)
      && (fshelp_isowner (&np->dn_stat, dircred->user) == EPERM))
    flags &= ~O_NOATIME;

  err = diskfs_make_peropen (np, (flags &~OPENONLY_STATE_MODES),
			       dircred->po, &newpo);

  if (! err)
    err = diskfs_create_protid (newpo, dircred->user, &newpi);

  if (! err)
    {
      newpo = 0;
      mach_port_t rendezvous = MACH_PORT_NULL;
      struct flock64 lock =
        {
         l_start: 0,
         l_len: 0,
         l_whence: SEEK_SET
       };

      if (flags & O_EXLOCK)
        {
         lock.l_type = F_WRLCK;
         err = fshelp_rlock_tweak (&np->userlock, &np->lock,
				   &newpi->po->lock_status, flags, 0, 0,
				   F_SETLK64, &lock, rendezvous);
       }
      else if (flags & O_SHLOCK)
        {
         lock.l_type = F_RDLCK;
         err = fshelp_rlock_tweak (&np->userlock, &np->lock,
				   &newpi->po->lock_status, flags, 0, 0,
				   F_SETLK64, &lock, rendezvous);
       }
    }

  if (! err)
    {
      free (newpi->po->path);
      if (dircred->po->path == NULL || !strcmp (dircred->po->path,"."))
	{
	  /* dircred is the root directory.  */
	  newpi->po->path = relpath;
	  relpath = NULL; /* Do not free relpath.  */
	}
      else
	{
	  newpi->po->path = NULL;
	  asprintf (&newpi->po->path, "%s/%s", dircred->po->path, relpath);
	}

      if (! newpi->po->path)
	err = errno;

      *retry_port = ports_get_right (newpi);
      ports_port_deref (newpi);
      newpi = 0;
    }

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

  if (newpi)
    ports_port_deref (newpi);
  if (newpo)
    diskfs_release_peropen (newpo);

  free (relpath);

  return err;
}
