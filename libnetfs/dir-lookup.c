/*
   Copyright (C) 1995,96,97,98,99,2000,01,02,13
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

#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <hurd/paths.h>
#include "netfs.h"
#include "fs_S.h"
#include "callbacks.h"
#include "misc.h"

error_t
netfs_S_dir_lookup (struct protid *diruser,
		    char *filename,
		    int flags,
		    mode_t mode,
		    retry_type *do_retry,
		    char *retry_name,
		    mach_port_t *retry_port,
		    mach_msg_type_name_t *retry_port_type)
{
  int create;			/* true if O_CREAT flag set */
  int excl;			/* true if O_EXCL flag set */
  int mustbedir = 0;		/* true if the result must be S_IFDIR */
  int lastcomp = 0;		/* true if we are at the last component */
  int newnode = 0;		/* true if this node is newly created */
  int nsymlinks = 0;
  struct node *dnp, *np;
  char *nextname;
  char *relpath;
  error_t error;
  struct protid *newpi;
  struct iouser *user;

  if (!diruser)
    return EOPNOTSUPP;

  create = (flags & O_CREAT);
  excl = (flags & O_EXCL);

  /* Skip leading slashes */
  while (*filename == '/')
    filename++;

  /* Preserve the path relative to diruser->po->path.  */
  relpath = strdup (filename);
  if (! relpath)
    return ENOMEM;

  *retry_port_type = MACH_MSG_TYPE_MAKE_SEND;
  *do_retry = FS_RETRY_NORMAL;
  *retry_name = '\0';

  if (*filename == '\0')
    {
      /* Set things up in the state expected by the code from gotit: on. */
      dnp = 0;
      np = diruser->po->np;
      pthread_mutex_lock (&np->lock);
      netfs_nref (np);
      goto gotit;
    }

  dnp = diruser->po->np;
  pthread_mutex_lock (&dnp->lock);

  netfs_nref (dnp);		/* acquire a reference for later netfs_nput */

  do
    {
      assert (!lastcomp);

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

    retry_lookup:

      if ((dnp == netfs_root_node || dnp == diruser->po->shadow_root)
	  && filename[0] == '.' && filename[1] == '.' && filename[2] == '\0')
	if (dnp == diruser->po->shadow_root)
	  /* We're at the root of a shadow tree.  */
	  {
	    *do_retry = FS_RETRY_REAUTH;
	    *retry_port = diruser->po->shadow_root_parent;
	    *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
	    if (! lastcomp)
	      strcpy (retry_name, nextname);
	    error = 0;
	    pthread_mutex_unlock (&dnp->lock);
	    goto out;
	  }
	else if (diruser->po->root_parent != MACH_PORT_NULL)
	  /* We're at a real translator root; even if DIRUSER->po has a
	     shadow root, we can get here if its in a directory that was
	     renamed out from under it...  */
	  {
	    *do_retry = FS_RETRY_REAUTH;
	    *retry_port = diruser->po->root_parent;
	    *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
	    if (!lastcomp)
	      strcpy (retry_name, nextname);
	    error = 0;
	    pthread_mutex_unlock (&dnp->lock);
	    goto out;
	  }
	else
	  /* We are global root */
	  {
	    error = 0;
	    np = dnp;
	    netfs_nref (np);
	  }
      else
	/* Attempt a lookup on the next pathname component. */
	error = netfs_attempt_lookup (diruser->user, dnp, filename, &np);

      /* At this point, DNP is unlocked */

      /* Implement O_EXCL flag here */
      if (lastcomp && create && excl && !error)
	error = EEXIST;

      /* Create the new node if necessary */
      if (lastcomp && create && error == ENOENT)
	{
	  mode &= ~(S_IFMT | S_ISPARE | S_ISVTX);
	  mode |= S_IFREG;
	  pthread_mutex_lock (&dnp->lock);
	  error = netfs_attempt_create_file (diruser->user, dnp,
					     filename, mode, &np);

	  /* If someone has already created the file (between our lookup
	     and this create) then we just got EEXIST.  If we are
	     EXCL, that's fine; otherwise, we have to retry the lookup. */
	  if (error == EEXIST && !excl)
	    {
	      pthread_mutex_lock (&dnp->lock);
	      goto retry_lookup;
	    }

	  newnode = 1;
	}

      /* All remaining errors get returned to the user */
      if (error)
	goto out;

      error = netfs_validate_stat (np, diruser->user);
      if (error)
	goto out;

      if ((((flags & O_NOTRANS) == 0) || !lastcomp)
	  && ((np->nn_translated & S_IPTRANS)
	      || S_ISFIFO (np->nn_translated)
	      || S_ISCHR (np->nn_translated)
	      || S_ISBLK (np->nn_translated)
	      || fshelp_translated (&np->transbox)))
	{
	  mach_port_t dirport;

	  /* A callback function for short-circuited translators.
	     S_ISLNK and S_IFSOCK are handled elsewhere. */
	  error_t short_circuited_callback1 (void *cookie1, void *cookie2,
					     uid_t *uid, gid_t *gid,
					     char **argz, size_t *argz_len)
	    {
	      struct node *np = cookie1;
	      error_t err;

	      err = netfs_validate_stat (np, diruser->user);
	      if (err)
		return err;

	      switch (np->nn_translated & S_IFMT)
		{
		case S_IFCHR:
		case S_IFBLK:
		  if (asprintf (argz, "%s%c%d%c%d",
				(S_ISCHR (np->nn_translated)
				 ? _HURD_CHRDEV : _HURD_BLKDEV),
				0, major (np->nn_stat.st_rdev),
				0, minor (np->nn_stat.st_rdev)) < 0)
		    return ENOMEM;
		  *argz_len = strlen (*argz) + 1;
		  *argz_len += strlen (*argz + *argz_len) + 1;
		  *argz_len += strlen (*argz + *argz_len) + 1;
		  break;
		case S_IFIFO:
		  if (asprintf (argz, "%s", _HURD_FIFO) < 0)
		    return ENOMEM;
		  *argz_len = strlen (*argz) + 1;
		  break;
		default:
		  return ENOENT;
		}

	      *uid = np->nn_stat.st_uid;
	      *gid = np->nn_stat.st_gid;

	      return 0;
	    }

	  /* Create an unauthenticated port for DNP, and then
	     unlock it. */
	  error = iohelp_create_empty_iouser (&user);
	  if (! error)
	    {
	      newpi = netfs_make_protid (netfs_make_peropen (dnp, 0,
							     diruser->po),
					 user);
	      if (! newpi)
	        {
		  error = errno;
		  iohelp_free_iouser (user);
		}
	    }

	  if (! error)
	    {
	      dirport = ports_get_send_right (newpi);
	      ports_port_deref (newpi);

	      error = fshelp_fetch_root (&np->transbox, diruser->po,
					 dirport,
					 diruser->user,
					 lastcomp ? flags : 0,
					 ((np->nn_translated & S_IPTRANS)
					 ? _netfs_translator_callback1
					   : short_circuited_callback1),
					 _netfs_translator_callback2,
					 do_retry, retry_name, retry_port);
	      /* fetch_root copies DIRPORT for success, so we always should
		 deallocate our send right.  */
	      mach_port_deallocate (mach_task_self (), dirport);
	    }

	  if (error != ENOENT)
	    {
	      netfs_nrele (dnp);
	      netfs_nput (np);
	      *retry_port_type = MACH_MSG_TYPE_MOVE_SEND;
	      if (!lastcomp && !error)
		{
		  strcat (retry_name, "/");
		  strcat (retry_name, nextname);
		}
	      return error;
	    }

	  /* ENOENT means there was a hiccup, and the translator vanished
	     while NP was unlocked inside fshelp_fetch_root; continue as normal. */
	  error = 0;
	}

      if (S_ISLNK (np->nn_translated)
	  && (!lastcomp
	      || mustbedir	/* "foo/" must see that foo points to a dir */
	      || !(flags & (O_NOLINK|O_NOTRANS))))
	{
	  size_t nextnamelen, newnamelen, linklen;
	  char *linkbuf;

	  /* Handle symlink interpretation */
	  if (nsymlinks++ > netfs_maxsymlinks)
	    {
	      error = ELOOP;
	      goto out;
	    }

	  linklen = np->nn_stat.st_size;

	  nextnamelen = nextname ? strlen (nextname) + 1 : 0;
	  newnamelen = nextnamelen + linklen + 1;
	  linkbuf = alloca (newnamelen);

	  error = netfs_attempt_readlink (diruser->user, np, linkbuf);
	  if (error)
	    goto out;

	  if (nextname)
	    {
	      linkbuf[linklen] = '/';
	      memcpy (linkbuf + linklen + 1, nextname,
		     nextnamelen - 1);
	    }
	  linkbuf[nextnamelen + linklen] = '\0';

	  if (linkbuf[0] == '/')
	    {
	      /* Punt to the caller */
	      *do_retry = FS_RETRY_MAGICAL;
	      *retry_port = MACH_PORT_NULL;
	      strcpy (retry_name, linkbuf);
	      goto out;
	    }

	  filename = linkbuf;
	  if (lastcomp)
	    {
	      lastcomp = 0;

	      /* Symlinks to nonexistent files aren't allowed to cause
		 creation, so clear the flag here. */
	      create = 0;
	    }
	  netfs_nput (np);
	  pthread_mutex_lock (&dnp->lock);
	  np = 0;
	}
      else
	{
	  /* Normal nodes here for next filename component */
	  filename = nextname;
	  netfs_nrele (dnp);

	  if (lastcomp)
	    dnp = 0;
	  else
	    {
	      dnp = np;
	      np = 0;
	    }
	}
    }
  while (filename && *filename);

  /* At this point, NP is the node to return.  */
 gotit:

  if (mustbedir)
    {
      netfs_validate_stat (np, diruser->user);
      if (!S_ISDIR (np->nn_stat.st_mode))
	{
	  error = ENOTDIR;
	  goto out;
	}
    }
  error = netfs_check_open_permissions (diruser->user, np,
					flags, newnode);
  if (error)
    goto out;

  flags &= ~OPENONLY_STATE_MODES;

  error = iohelp_dup_iouser (&user, diruser->user);
  if (error)
    goto out;

  newpi = netfs_make_protid (netfs_make_peropen (np, flags, diruser->po),
			     user);
  if (! newpi)
    {
      iohelp_free_iouser (user);
      error = errno;
      goto out;
    }

  free (newpi->po->path);
  if (diruser->po->path == NULL)
    {
      /* diruser is the root directory.  */
      newpi->po->path = relpath;
      relpath = NULL; /* Do not free relpath.  */
    }
  else
    {
      newpi->po->path = NULL;
      asprintf (&newpi->po->path, "%s/%s", diruser->po->path, relpath);
    }

  if (! newpi->po->path)
    error = errno;

  *retry_port = ports_get_right (newpi);
  ports_port_deref (newpi);

 out:
  if (np)
    netfs_nput (np);
  if (dnp)
    netfs_nrele (dnp);
  free (relpath);
  return error;
}
