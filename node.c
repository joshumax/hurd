/* procfs -- a translator for providing GNU/Linux compatible 
             proc pseudo-filesystem

   node.c -- This file contains function defintions to handle
             node creation and destruction.
               
   Copyright (C) 2008, FSF.
   Written as a Summer of Code Project
   
   procfs is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   procfs is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. 
*/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <hurd/ihash.h>
#include <hurd/fshelp.h>
#include <hurd/iohelp.h>

#include <hurd/netfs.h>

#include "procfs.h"

/* Return a new node in NODE, with a name NAME, and return the
   new node with a single reference in NODE. */
error_t procfs_create_node (struct procfs_dir_entry *dir_entry, 
                            const char *fs_path, struct node **node)
{
  struct node *new;
  struct netnode *nn = malloc (sizeof (struct netnode));
  error_t err;

  if (! nn)
    return ENOMEM;
  if (! fs_path)
    fs_path = strdup ("");
  nn->fs = dir_entry->dir->fs;
  nn->dir_entry = dir_entry;
  nn->dir = NULL;
  nn->fs_path = strdup (fs_path);

  new = netfs_make_node (nn);
  if (! new)
    {
      free (nn);
      return ENOMEM;
    }

  fshelp_touch (&new->nn_stat, TOUCH_ATIME|TOUCH_MTIME|TOUCH_CTIME,
		procfs_maptime);

  spin_lock (&nn->fs->inode_mappings_lock);
  err = hurd_ihash_add (&nn->fs->inode_mappings, dir_entry->stat.st_ino, dir_entry);
  spin_unlock (&nn->fs->inode_mappings_lock);

  if (err)
    {
      free (nn);
      free (new);
      return err;
    }
 
  dir_entry->node = new;
  *node = new;

  return 0;
}

/* Update the directory entry for NAME to reflect ST and SYMLINK_TARGET.
   True is returned if successful, or false if there was a memory allocation
   error.  TIMESTAMP is used to record the time of this update.  */
static void
update_entry (struct procfs_dir_entry *dir_entry, const struct stat *st,
	      const char *symlink_target, time_t timestamp)
{
  ino_t ino;
  struct procfs *fs = dir_entry->dir->fs;

  if (dir_entry->stat.st_ino)
    ino = dir_entry->stat.st_ino;
  else
    ino = fs->next_inode++;

  dir_entry->name_timestamp = timestamp;

  if (st)
    /* The ST and SYMLINK_TARGET parameters are only valid if ST isn't 0.  */
    {
      dir_entry->stat = *st;
      dir_entry->stat_timestamp = timestamp;

      if (!dir_entry->symlink_target || !symlink_target
	  || strcmp (dir_entry->symlink_target, symlink_target) != 0)
	{
	  if (dir_entry->symlink_target)
	    free (dir_entry->symlink_target);
	  dir_entry->symlink_target = symlink_target ? strdup (symlink_target) : 0;
	}
    }

  /* The st_ino field is always valid.  */
  dir_entry->stat.st_ino = ino;
  dir_entry->stat.st_fsid = fs->fsid;
  dir_entry->stat.st_fstype = PROCFILESYSTEM;
}

/* Refresh stat information for NODE */
error_t procfs_refresh_node (struct node *node)
{
  struct netnode *nn = node->nn;
  struct procfs_dir_entry *dir_entry = nn->dir_entry;

  if (! dir_entry)
    /* This is a deleted node, don't attempt to do anything.  */
    return 0;
  else
    {
      error_t err = 0;
      
      struct timeval tv;
      maptime_read (procfs_maptime, &tv); 
  
      time_t timestamp = tv.tv_sec;

      struct procfs_dir *dir = dir_entry->dir;

      mutex_lock (&dir->node->lock);

      if (! dir_entry->self_p)
	/* This is a deleted entry, just awaiting disposal; do so.  */
	{
#if 0
	  nn->dir_entry = 0;
	  free_entry (dir_entry);
	  return 0;
#endif
	}
      
      else if (dir_entry->noent)
	err = ENOENT;
      else 
        {
          if (*(dir_entry->name))
            {
              err =  procfs_dir_refresh (dir_entry->dir, 
              dir_entry->dir->node == dir_entry->dir->fs->root);
	      if (!err && dir_entry->noent)
	        err = ENOENT;

              if (err == ENOENT)
	        {
	          dir_entry->noent = 1; /* A negative entry.  */
	          dir_entry->name_timestamp = timestamp;
	        }
            }
          else
	    {
	      /* Refresh the root node with the old stat
                 information.  */             
              update_entry (dir_entry, &netfs_root_node->nn_stat, NULL, timestamp);     
	    }
	}

      node->nn_stat = dir_entry->stat;
      node->nn_translated = S_ISLNK (dir_entry->stat.st_mode) ? S_IFLNK : 0;
      if (!nn->dir && S_ISDIR (dir_entry->stat.st_mode))
	procfs_dir_create (nn->fs, node, nn->fs_path, &nn->dir);

      mutex_unlock (&dir->node->lock);

      return err;
    }
}

/* Remove NODE from its entry */
error_t procfs_remove_node (struct node *node)
{

    /* STUB */
    
  return 0;    
}
