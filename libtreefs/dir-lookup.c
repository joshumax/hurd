/* Default treefs_s_dir_lookup hook

   Copyright (C) 1992, 1993, 1994, 1995, 1998 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <fcntl.h>
#include <string.h>

#include <hurd/fsys.h>

#include "treefs.h"
#include "treefs-s-hooks.h"

/* Default dir_lookup hook.  This code was originally copied from diskfs.  */ 
error_t
_treefs_s_dir_lookup (struct treefs_handle *h,
		      char *path, int flags, mode_t mode,
		      enum retry_type *retry, char *retry_name,
		      file_t *result, mach_msg_type_name_t *result_type)
{
  struct treefs_node *dir;
  struct treefs_node *node;
  unsigned symlink_expansions = 0;
  error_t err = 0;
  char *path_buf = 0;
  int path_buf_len = 0;
  int lastcomp = 0;
  int mustbedir = 0;

  flags &= O_HURD;
  mode &= ~S_IFMT;
  
  /* Skip leading slashes */
  while (path[0] == '/')
    path++;

  *result_type = MACH_MSG_TYPE_MAKE_SEND;
  *retry = FS_RETRY_NORMAL;
  retry_name[0] = '\0';

  if (path[0] == '\0')
    {
      /* Set things up in the state expected by the code from gotit: on. */
      dir = 0;
      node = h->po->node;
      pthread_mutex_lock (&node->lock);
      treefs_node_ref (node);
      goto gotit;
    }

  dir = h->po->node;
  pthread_mutex_lock (&dir->lock);
  node = 0;

  treefs_node_ref (dir);	/* acquire a ref for later node_release */

  do
    {
      char *nextname;

      assert_backtrace (!lastcomp);
      
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
	      
	    }
	  else
	    lastcomp = 0;
	}
      else
	lastcomp = 1;
	  
      node = 0;

      /* Lookup the next pathname component.  */
      if (!lastcomp)
	err = treefs_dir_lookup (dir, path, h->auth, 0, 0, &node);
      else
	/* ... and in this case, the last.  Note that the S_IFREG only
	   applies in the case of O_CREAT, which is turned off for
	   directories anyway.  */
	err =
	  treefs_dir_lookup (dir, path, h->auth, flags, mode | S_IFREG, &node);

      /* If we get an error we're done */
      if (err == EAGAIN)
	{
	  if (h->po->parent_port != MACH_PORT_NULL)
	    {
	      *retry = FS_RETRY_REAUTH;
	      *result = h->po->parent_port;
	      *result_type = MACH_MSG_TYPE_COPY_SEND;
	      if (!lastcomp)
		strcpy (retry_name, nextname);
	      err = 0;
	      goto out;
	    }
	  else
	    /* The global filesystem root...  .. == .  */
	    {
	      err = 0;
	      node = dir;
	      treefs_node_ref (node);
	    }
	}
      
      if (err)
	goto out;

      /* If this is translated, start the translator (if necessary)
	 and return.  */
      /* The check for `node != dir' simplifies this code a great
	 deal.  Such a translator should already have been started,
	 so there's no lossage in doing it this way. */
      if ((!lastcomp || !(flags & O_NOTRANS))
	  && node != dir)
	{
	  file_t dir_port = MACH_PORT_NULL, child_fsys;

	  /* Be very careful not to hold an inode lock while fetching
	     a translator lock and vice versa.  */

	  pthread_mutex_unlock (&node->lock);
	  pthread_mutex_unlock (&dir->lock);

	  do
	    {
	      err =
		treefs_node_get_active_trans (node, dir, h->po->parent_port,
					      &dir_port, &child_fsys);
	      if (err == 0 && child_fsys != MACH_PORT_NULL)
		{
		  err =
		    fsys_getroot (child_fsys, dir_port,
				  MACH_MSG_TYPE_COPY_SEND,
				  h->auth->uids, h->auth->nuids,
				  h->auth->gids, h->auth->ngids,
				  lastcomp ? flags : 0,
				  retry, retry_name, result);
		  /* If we got MACH_SEND_INVALID_DEST or MIG_SERVER_DIED, then
		     the server is dead.  Zero out the old control port and try
		     everything again.  */
		  if (err == MACH_SEND_INVALID_DEST || err == EMIG_SERVER_DIED)
		    treefs_node_drop_active_trans (node, child_fsys);
		}
	    }
	  while (err == MACH_SEND_INVALID_DEST || err == EMIG_SERVER_DIED);

	  if (err || child_fsys)
	    {
	      /* We're done; return to the user.  If there are more
		 components after this name, be sure to append them to the
		 user's retry path. */
	      if (!err && !lastcomp)
		{
		  strcat (retry_name, "/");
		  strcat (retry_name, nextname);
		}

	      *result_type = MACH_MSG_TYPE_MOVE_SEND;

	      treefs_node_unref (dir);
	      treefs_node_unref (node);
	      if (dir_port)
		mach_port_deallocate (mach_task_self (), dir_port);

	      return err;
	    }
	  
	  /* We're here if we tried the translator check, and it
	     failed.   Lock everything back, and make sure we do it
	     in the right order. */
	  if (strcmp (path, "..") != 0)
	    {
	      if (pthread_mutex_trylock (&dir->lock))
	        {
		  pthread_mutex_unlock (&node->lock);
		  pthread_mutex_lock (&dir->lock);
		  pthread_mutex_lock (&node->lock);
		}
	    }
	  else
	    pthread_mutex_lock (&dir->lock);
	}
      
      if (treefs_node_type (node) == S_IFLNK
	  && !(lastcomp && (flags & (O_NOLINK|O_NOTRANS))))
	/* Handle symlink interpretation */
	{
	  unsigned nextname_len = nextname ? strlen (nextname) + 1 : 0;
	  /* max space we currently have for the sym link */
	  unsigned sym_len = path_buf_len - nextname_len - 1;

	  if (symlink_expansions++ > node->fsys->max_symlinks)
	    {
	      err = ELOOP;
	      goto out;
	    }
	
	  err = treefs_node_get_symlink (node, path_buf, &sym_len);
	  if (err == E2BIG)
	    /* Symlink contents + extra path won't fit in our buffer, so
	       reallocate it and try again.  */
	    {
	      path_buf_len = sym_len + nextname_len + 1 + 1;
	      path_buf = alloca (path_buf_len);
	      err = treefs_node_get_symlink (node, path_buf, &sym_len);
	    }
	  if (err)
	    goto out;

	  if (nextname)
	    {
	      path_buf[sym_len] = '/';
	      bcopy (nextname, path_buf + sym_len + 1, nextname_len - 1);
	    }
	  if (mustbedir)
	    {
	      path_buf[nextnamelen + sym_len] = '/';
	      path_buf[nextnamelen + sym_len + 1] = '\0';
	    }
	  else
	    path_buf[nextname_len + sym_len] = '\0';

	  if (path_buf[0] == '/')
	    {
	      /* Punt to the caller.  */
	      *retry = FS_RETRY_MAGICAL;
	      *result = MACH_PORT_NULL;
	      strcpy (retry_name, path_buf);
	      goto out;
	    }
	  
	  path = path_buf;
	  mustbedir = 0;
	  if (lastcomp)
	    {
	      lastcomp = 0;
	      /* Symlinks to nonexistent files aren't allowed to cause
		 creation, so clear the flag here. */
	      flags &= ~O_CREAT;
	    }
	  treefs_node_release (node);
	  node = 0;
	}
      else
	{
	  /* Handle normal nodes */
	  path = nextname;
	  if (node == dir)
	    treefs_node_unref (dir);
	  else
	    treefs_node_release (dir);
	  if (!lastcomp)
	    {
	      dir = node;
	      node = 0;
	    }
	  else
	    dir = 0;
	}
    } while (path && *path);
  
 gotit:
  /* At this point, node is the node to return.  */

  if (mustbedir && !treefs_node_isdir (node))
    err = ENOTDIR;
  if (err)
    goto out;

  err = treefs_node_create_right (node, flags, h->po->parent_port, h->auth,
				  result);

 out:
  if (node)
    {
      if (dir == node)
	treefs_node_unref (node);
      else
	treefs_node_release (node);
    }
  if (dir)
    treefs_node_release (dir);
  return err;  
}
