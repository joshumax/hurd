/* Directory operations

   Copyright (C) 1997,98,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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

#include <unistd.h>
#include <string.h>

#include <hurd/netfs.h>

#include "ftpfs.h"
#include "ccache.h"

/* Free the directory entry E and all resources it consumes.  */
void
free_entry (struct ftpfs_dir_entry *e)
{
  assert_backtrace (e->deleted);
  free (e->name);
  if (e->symlink_target)
    free (e->symlink_target);
  free (e);
}

/* Calculate NAME_PTR's hash value.  */
static hurd_ihash_key_t
ihash_hash (const void *name_ptr)
{
  const char *name = (const char *) name_ptr;
  return (hurd_ihash_key_t) hurd_ihash_hash32 (name, strlen (name), 0);
}

/* Compare two names which are used as keys.  */
static int
ihash_compare (const void *key1, const void *key2)
{
  const char *name1 = (const char *) key1;
  const char *name2 = (const char *) key2;

  return strcmp (name1, name2) == 0;
}

/* Lookup NAME in DIR and return its entry.  If there is no such entry, and
   ADD is true, then a new entry is allocated and returned, otherwise 0 is
   returned (if ADD is true then 0 can be returned if a memory allocation
   error occurs).  */
struct ftpfs_dir_entry *
lookup (struct ftpfs_dir *dir, const char *name, int add)
{
  struct ftpfs_dir_entry *e =
    hurd_ihash_find (&dir->htable, (hurd_ihash_key_t) name);

  if (!e && add)
    {
      e = malloc (sizeof *e);
      if (e)
	{
	  e->name = strdup (name);
	  e->node = 0;
	  e->dir = dir;
	  e->stat_timestamp = 0;
	  memset (&e->stat, 0, sizeof e->stat);
	  e->symlink_target = 0;
	  e->noent = 0;
	  e->valid = 0;
          e->deleted = 0;
	  e->name_timestamp = e->stat_timestamp = 0;
	  e->ordered_next = 0;
	  e->ordered_self_p = 0;
          hurd_ihash_add (&dir->htable, (hurd_ihash_key_t) e->name, e);
	}
    }

  return e;
}

/* Remove E from its position in the ordered_next chain.  */
static void
ordered_unlink (struct ftpfs_dir_entry *e)
{
  if (e->ordered_self_p)
    *e->ordered_self_p = e->ordered_next;
  if (e->ordered_next)
    e->ordered_next->ordered_self_p = e->ordered_self_p;
}

/* Delete E from its directory, freeing any resources it holds.  */
static void
delete (struct ftpfs_dir_entry *e, struct ftpfs_dir *dir)
{
  /* This indicates a deleted entry.  */
  e->deleted = 1;

  /* Take out of the directory ordered list.  */
  ordered_unlink (e);

  hurd_ihash_locp_remove (&dir->htable, e->dir_locp);

  /* If there's a node attached, we'll delete the entry whenever it goes
     away, otherwise, just delete it now.  */
  if (! e->node)
    free_entry (e);
}

/* Clear the valid bit in all DIR's htable.  */
static void
mark (struct ftpfs_dir *dir)
{
  HURD_IHASH_ITERATE (&dir->htable, value)
    {
      struct ftpfs_dir_entry *e = (struct ftpfs_dir_entry *) value;
      e->valid = 0;
    }
}

/* Delete any entries in DIR which don't have their valid bit set.  */
static void
sweep (struct ftpfs_dir *dir)
{
  HURD_IHASH_ITERATE (&dir->htable, value)
    {
      struct ftpfs_dir_entry *e = (struct ftpfs_dir_entry *) value;
      if (!e->valid && !e->noent)
        delete (e, dir);
    }
}

/* Update the directory entry for NAME to reflect ST and SYMLINK_TARGET.
   True is returned if successful, or false if there was a memory allocation
   error.  TIMESTAMP is used to record the time of this update.  */
static void
update_entry (struct ftpfs_dir_entry *e, const struct stat *st,
	      const char *symlink_target, time_t timestamp)
{
  ino_t ino;
  struct ftpfs *fs = e->dir->fs;

  if (e->stat.st_ino)
    ino = e->stat.st_ino;
  else
    ino = fs->next_inode++;

  e->name_timestamp = timestamp;

  if (st)
    /* The ST and SYMLINK_TARGET parameters are only valid if ST isn't 0.  */
    {
      e->stat = *st;
      e->stat_timestamp = timestamp;

      if (!e->symlink_target || !symlink_target
	  || strcmp (e->symlink_target, symlink_target) != 0)
	{
	  if (e->symlink_target)
	    free (e->symlink_target);
	  e->symlink_target = symlink_target ? strdup (symlink_target) : 0;
	}
    }

  /* The st_ino field is always valid.  */
  e->stat.st_ino = ino;
  e->stat.st_fsid = fs->fsid;
  e->stat.st_fstype = FSTYPE_FTP;
}

/* Add the timestamp TIMESTAMP to the set used to detect bulk stats, and
   return true if there have been enough individual stats recently to call
   for just refetching the whole directory.  */
static int
need_bulk_stat (time_t timestamp, struct ftpfs_dir *dir)
{
  time_t period = dir->fs->params.bulk_stat_period;
  unsigned threshold = dir->fs->params.bulk_stat_threshold;

  if (timestamp > dir->bulk_stat_base_stamp + period * 3)
    /* No stats done in a while, just start over.  */
    {
      dir->bulk_stat_count_first_half = 1;
      dir->bulk_stat_count_second_half = 0;
      dir->bulk_stat_base_stamp = (timestamp / period) * period;
    }
  else if (timestamp > dir->bulk_stat_base_stamp + period * 2)
    /* Start a new period, but keep the second half of the old one.  */
    {
      dir->bulk_stat_count_first_half = dir->bulk_stat_count_second_half;
      dir->bulk_stat_count_second_half = 1;
      dir->bulk_stat_base_stamp += period;
    }
  else if (timestamp > dir->bulk_stat_base_stamp + period)
    dir->bulk_stat_count_second_half++;
  else
    dir->bulk_stat_count_first_half++;

  return
    (dir->bulk_stat_count_first_half + dir->bulk_stat_count_second_half)
    > threshold;
}

static void
reset_bulk_stat_info (struct ftpfs_dir *dir)
{
  dir->bulk_stat_count_first_half = 0;
  dir->bulk_stat_count_second_half = 0;
  dir->bulk_stat_base_stamp = 0;
}

/* State shared between ftpfs_dir_refresh and update_ordered_entry.  */
struct dir_fetch_state
{
  struct ftpfs_dir *dir;
  time_t timestamp;

  /* A pointer to the NEXT-field of the previously seen entry, or a pointer
     to the ORDERED field in the directory if this is the first.  */
  struct ftpfs_dir_entry **prev_entry_next_p;
};

/* Update the directory entry for NAME to reflect ST and SYMLINK_TARGET, also
   rearranging the entries to reflect the order in which they are sent from
   the server, and setting their valid bits so that obsolete entries can be
   deleted.  HOOK points to the state from ftpfs_dir_fetch.  */
static error_t
update_ordered_entry (const char *name, const struct stat *st,
		      const char *symlink_target, void *hook)
{
  struct dir_fetch_state *dfs = hook;
  struct ftpfs_dir_entry *e = lookup (dfs->dir, name, 1);

  if (! e)
    return ENOMEM;

  update_entry (e, st, symlink_target, dfs->timestamp);
  e->valid = 1;

  if (! e->ordered_self_p)
    /* Position E in the ordered chain following the previously seen entry.  */
    {
      /* The PREV_ENTRY_NEXT_P field holds a pointer to the NEXT-field of the
	 previous entry, or a pointer to the ORDERED field in the directory. */
      e->ordered_self_p = dfs->prev_entry_next_p;

      if (*e->ordered_self_p)
	/* Update the self_p pointer of the previous successor.  */
	(*e->ordered_self_p)->ordered_self_p = &e->ordered_next;

      /* E comes before the previous successor.  */
      e->ordered_next = *e->ordered_self_p;

      *e->ordered_self_p = e;	/* Put E there.  */
    }

  /* Put the next entry after this one. */
  dfs->prev_entry_next_p = &e->ordered_next;

  return 0;
}

/* Update the directory entry for NAME, rearranging the entries to reflect
   the order in which they are sent from the server, and setting their valid
   bits so that obsolete entries can be deleted.  HOOK points to the state
   from ftpfs_dir_fetch.  */
static error_t
update_ordered_name (const char *name, void *hook)
{
  /* We just do the same thing as for stats, but without the stat info.  */
  return update_ordered_entry (name, 0, 0, hook);
}

/* Refresh DIR from the directory DIR_NAME in the filesystem FS.  If
   UPDATE_STATS is true, then directory stat information will also be
   updated.  If PRESERVE_ENTRY is non-0, that entry won't be deleted if it's
   not in the directory after the refresh, but instead will have its NOENT
   flag turned on.  */
static error_t
refresh_dir (struct ftpfs_dir *dir, int update_stats, time_t timestamp,
	     struct ftpfs_dir_entry *preserve_entry)
{
  error_t err;
  struct ftp_conn *conn;
  struct dir_fetch_state dfs;

  if ((update_stats
       ? dir->stat_timestamp + dir->fs->params.stat_timeout
       : dir->name_timestamp + dir->fs->params.name_timeout)
      >= timestamp)
    /* We've already refreshed this directory recently.  */
    return 0;

  err = ftpfs_get_ftp_conn (dir->fs, &conn);
  if (err)
    return err;

  /* Mark directory entries so we can GC them later using sweep.  */
  mark (dir);

  if (update_stats)
    /* We're doing a bulk stat now, so don't do another for a while.  */
    reset_bulk_stat_info (dir);

  /* Info passed to update_ordered_entry.  */
  dfs.dir = dir;
  dfs.timestamp = timestamp;
  dfs.prev_entry_next_p = &dir->ordered;

  /* Make sure `.' and `..' are always included (if the actual list also
     includes `.' and `..', the ordered may be rearranged).  */
  err = update_ordered_name (".", &dfs);
  if (! err)
    err = update_ordered_name ("..", &dfs);

  if (! err)
    {
      /* Refetch the directory from the server.  */
      if (update_stats)
	/* Fetch both names and stat info.  */
	err = ftp_conn_get_stats (conn, dir->rmt_path, 1,
				  update_ordered_entry, &dfs);
      else
	/* Just fetch names.  */
	err = ftp_conn_get_names (conn, dir->rmt_path,
				  update_ordered_name, &dfs);
    }

  if (! err)
    /* GC any directory entries that weren't seen this time.  */
    {
      dir->name_timestamp = timestamp;
      if (update_stats)
	dir->stat_timestamp = timestamp;
      if (preserve_entry && !preserve_entry->valid)
	{
	  preserve_entry->noent = 1;
	  preserve_entry->name_timestamp = timestamp;
	}
      sweep (dir);
    }

  ftpfs_release_ftp_conn (dir->fs, conn);

  return err;
}

/* Refresh DIR.  */
error_t
ftpfs_dir_refresh (struct ftpfs_dir *dir)
{
  time_t timestamp = NOW;
  return refresh_dir (dir, 0, timestamp, 0);
}

/* State shared between ftpfs_dir_entry_refresh and update_old_entry.  */
struct refresh_entry_state
{
  struct ftpfs_dir_entry *entry;
  time_t timestamp;
};

/* Update the directory entry for NAME to reflect ST and SYMLINK_TARGET.
   HOOK points to the state from ftpfs_dir_fetch_entry.  */
static error_t
update_old_entry (const char *name, const struct stat *st,
		  const char *symlink_target, void *hook)
{
  struct refresh_entry_state *res = hook;

  if (strcmp (name, res->entry->name) != 0)
    return EGRATUITOUS;

  update_entry (res->entry, st, symlink_target, res->timestamp);

  return 0;
}

/* Refresh stat information for NODE.  This may actually refresh the whole
   directory if that is deemed desirable.  NODE should be locked.  */
error_t
ftpfs_refresh_node (struct node *node)
{
  struct netnode *nn = node->nn;
  struct ftpfs_dir_entry *entry = nn->dir_entry;

  if (! entry)
    /* This is a deleted node, don't attempt to do anything.  */
    return 0;
  else
    {
      error_t err = 0;
      time_t timestamp = NOW;
      struct ftpfs_dir *dir = entry->dir;

      pthread_mutex_lock (&dir->node->lock);

      if (entry->deleted)
	/* This is a deleted entry, just awaiting disposal; do so.  */
	{
	  nn->dir_entry = 0;
	  free_entry (entry);
	  return 0;
	}
      else if ((entry->name_timestamp + dir->fs->params.name_timeout
		>= timestamp)
	       && entry->noent)
	err = ENOENT;
      else if (entry->stat_timestamp + dir->fs->params.stat_timeout < timestamp)
	{
	  /* Stat information needs updating.  */
	  if (need_bulk_stat (timestamp, dir))
	    /* Refetch the whole directory from the server.  */
	    {
	      err =  refresh_dir (entry->dir, 1, timestamp, entry);
	      if (!err && entry->noent)
		err = ENOENT;
	    }
	  else if (*(entry->name))
	    {
	      /* The root node is treated separately below.  */
	      struct ftp_conn *conn;

	      err = ftpfs_get_ftp_conn (dir->fs, &conn);

	      if (! err)
		{
		  char *rmt_path;

		  err = ftp_conn_append_name (conn, dir->rmt_path, entry->name,
					      &rmt_path);
		  if (! err)
		    {
		      struct refresh_entry_state res;

		      res.entry = entry;
		      res.timestamp = timestamp;

		      if (! err)
			err = ftp_conn_get_stats (conn, rmt_path, 0,
						  update_old_entry, &res);

		      free (rmt_path);
		    }

		  ftpfs_release_ftp_conn (dir->fs, conn);
		}

	      if (err == ENOENT)
		{
		  entry->noent = 1; /* A negative entry.  */
		  entry->name_timestamp = timestamp;
		}
	    }
	  else
	    {
	      /* Refresh the root node with the old stat
                 information.  */
	      struct refresh_entry_state res;
	      res.entry = entry;
	      res.timestamp = timestamp;
	      err = update_old_entry (entry->name,
				      &netfs_root_node->nn_stat,
				      NULL, &res);
	    }
	}

      if ((entry->stat.st_mtim.tv_sec < node->nn_stat.st_mtim.tv_sec
           || (entry->stat.st_mtim.tv_sec == node->nn_stat.st_mtim.tv_sec
               && entry->stat.st_mtim.tv_nsec < node->nn_stat.st_mtim.tv_nsec)
	   || entry->stat.st_size != node->nn_stat.st_size)
	  && nn && nn->contents)
	/* The file has changed.  */
	ccache_invalidate (nn->contents);

      node->nn_stat = entry->stat;
      node->nn_translated = S_ISLNK (entry->stat.st_mode) ? S_IFLNK : 0;
      if (!nn->dir && S_ISDIR (entry->stat.st_mode))
	ftpfs_dir_create (nn->fs, node, nn->rmt_path, &nn->dir);

      pthread_mutex_unlock (&dir->node->lock);

      ftpfs_cache_node (node);

      return err;
    }
}

/* Remove NODE from its entry (if the entry is still valid, it will remain
   without a node).  NODE should be locked.  */
error_t
ftpfs_detach_node (struct node *node)
{
  struct netnode *nn = node->nn;
  struct ftpfs_dir_entry *entry = nn->dir_entry;

  if (entry)
    /* NODE is still attached to some entry, so detach it.  */
    {
      struct ftpfs_dir *dir = entry->dir;

      pthread_mutex_lock (&dir->node->lock);

      if (! entry->deleted)
	/* Just detach NODE from the still active entry.  */
	entry->node = 0;
      else
	/* This is a deleted entry, just awaiting disposal; do so.  */
	{
	  nn->dir_entry = 0;
	  free_entry (entry);
	}

      if (--dir->num_live_entries == 0)
	netfs_nput (dir->node);
      else
	pthread_mutex_unlock (&dir->node->lock);
    }

  return 0;
}

/* State shared between ftpfs_dir_lookup and update_new_entry.  */
struct new_entry_state
{
  time_t timestamp;
  struct ftpfs_dir *dir;
  struct ftpfs_dir_entry *entry;
};

/* Update the directory entry for NAME to reflect ST and SYMLINK_TARGET.
   HOOK->entry will be updated to reflect the new entry.  */
static error_t
update_new_entry (const char *name, const struct stat *st,
		  const char *symlink_target, void *hook)
{
  struct ftpfs_dir_entry *e;
  struct new_entry_state *nes = hook;

  e = lookup (nes->dir, name, 1);
  if (! e)
    return ENOMEM;

  update_entry (e, st, symlink_target, nes->timestamp);
  nes->entry = e;

  return 0;
}

/* Lookup NAME in DIR, returning its entry, or an error.  DIR's node should
   be locked, and will be unlocked after returning; *NODE will contain the
   result node, locked, and with an additional reference, or 0 if an error
   occurs.  */
error_t
ftpfs_dir_lookup (struct ftpfs_dir *dir, const char *name,
		  struct node **node)
{
  struct ftp_conn *conn;
  struct ftpfs_dir_entry *e;
  error_t err = 0;
  char *rmt_path = 0;
  time_t timestamp = NOW;

  if (*name == '\0' || strcmp (name, ".") == 0)
    /* Current directory -- just add an additional reference to DIR's node
       and return it.  */
    {
      netfs_nref (dir->node);
      *node = dir->node;
      return 0;
    }
  else if (strcmp (name, "..") == 0)
    /* Parent directory.  */
    {
      if (dir->node->nn->dir_entry)
	{
	  *node = dir->node->nn->dir_entry->dir->node;
	  pthread_mutex_lock (&(*node)->lock);
	  netfs_nref (*node);
	}
      else
	{
	  err = ENOENT;		/* No .. */
	  *node = 0;
	}

      pthread_mutex_unlock (&dir->node->lock);

      return err;
    }

  e = lookup (dir, name, 0);
  if (!e || e->name_timestamp + dir->fs->params.name_timeout < timestamp)
    /* Try to fetch info about NAME.  */
    {
      if (need_bulk_stat (timestamp, dir))
	/* Refetch the whole directory from the server.  */
	{
	  err =  refresh_dir (dir, 1, timestamp, e);
	  if (!err && !e)
	    e = lookup (dir, name, 0);
	}
      else
	{
	  err = ftpfs_get_ftp_conn (dir->fs, &conn);
	  if (! err)
	    {
	      err = ftp_conn_append_name (conn, dir->rmt_path, name,
					  &rmt_path);
	      if (! err)
		{
		  struct new_entry_state nes;

		  nes.dir = dir;
		  nes.timestamp = timestamp;
		  nes.entry = NULL;

		  err = ftp_conn_get_stats (conn, rmt_path, 0,
					    update_new_entry, &nes);
		  if (! err)
		    e = nes.entry;
		  else if (err == ENOENT)
		    {
		      e = lookup (dir, name, 1);
		      if (! e)
			err = ENOMEM;
		      else
			{
			  e->noent = 1;	/* A negative entry.  */
			  e->name_timestamp = timestamp;
			}
		    }
		}

	      ftpfs_release_ftp_conn (dir->fs, conn);
	    }
	}
    }

  if (! err)
    {
      if (e && !e->noent)
	/* We've got a dir entry, get a node for it.  */
	{
	  /* If there's already a node, add a ref so that it doesn't go
             away.  */
          if (e->node)
            netfs_nref (e->node);

	  if (! e->node)
	    /* No node; make one and install it into E.  */
	    {
	      if (! rmt_path)
		/* We have to cons up the absolute path.  We need the
		   connection just for the pathname frobbing functions.  */
		{
		  err = ftpfs_get_ftp_conn (dir->fs, &conn);
		  if (! err)
		    {
		      err = ftp_conn_append_name (conn, dir->rmt_path, name,
						  &rmt_path);
		      ftpfs_release_ftp_conn (dir->fs, conn);
		    }
		}

	      if (! err)
		{
		  err = ftpfs_create_node (e, rmt_path, &e->node);

		  if (!err && dir->num_live_entries++ == 0)
		    /* Keep a reference to dir's node corresponding to
		       children.  */
                    netfs_nref (dir->node);
		}
	    }

	  if (! err)
	    {
	      *node = e->node;
	      /* We have to unlock DIR's node before locking the child node
		 because the locking order is always child-parent.  We know
		 the child node won't go away because we already hold the
		 additional reference to it.  */
	      pthread_mutex_unlock (&dir->node->lock);
	      pthread_mutex_lock (&e->node->lock);
	    }
	}
      else
	err = ENOENT;
    }

  if (err)
    {
      *node = 0;
      pthread_mutex_unlock (&dir->node->lock);
    }

  free (rmt_path);

  return err;
}

/* Lookup the null name in DIR, and return a node for it in NODE.  Unlike
   ftpfs_dir_lookup, this won't attempt to validate the existence of the
   entry (to avoid opening a new connection if possible) -- that will happen
   the first time the entry is refreshed.  Also unlink ftpfs_dir_lookup, this
   function doesn't expect DIR to be locked, and won't return *NODE locked.
   This function is only used for bootstrapping the root node.  */
error_t
ftpfs_dir_null_lookup (struct ftpfs_dir *dir, struct node **node)
{
  struct ftpfs_dir_entry *e;
  error_t err = 0;

  e = lookup (dir, "", 1);
  if (! e)
    return ENOMEM;

  if (! e->noent)
    /* We've got a dir entry, get a node for it.  */
    {
      /* If there's already a node, add a ref so that it doesn't go away.  */
      if (e->node)
        netfs_nref (e->node);

      if (! e->node)
	/* No node; make one and install it into E.  */
	{
	  err = ftpfs_create_node (e, dir->rmt_path, &e->node);

	  if (!err && dir->num_live_entries++ == 0)
	    /* Keep a reference to dir's node corresponding to children.  */
            netfs_nref (dir->node);
	}

      if (! err)
	*node = e->node;
    }
  else
    err = ENOENT;

  return err;
}

/* Size of initial htable for a new directory.  */
#define INIT_HTABLE_LEN 5

/* Return in DIR a new ftpfs directory, in the filesystem FS, with node NODE
   and remote path RMT_PATH.  RMT_PATH is *not copied*, so it shouldn't ever
   change while this directory is active.  */
error_t
ftpfs_dir_create (struct ftpfs *fs, struct node *node, const char *rmt_path,
		  struct ftpfs_dir **dir)
{
  struct ftpfs_dir *new = malloc (sizeof (struct ftpfs_dir));

  if (! new)
    {
      free (new);
      return ENOMEM;
    }

  netfs_nref (node);

  hurd_ihash_init (&new->htable, offsetof (struct ftpfs_dir_entry, dir_locp));
  hurd_ihash_set_gki (&new->htable, ihash_hash, ihash_compare);
  new->num_live_entries = 0;
  new->ordered = 0;
  new->rmt_path = rmt_path;
  new->fs = fs;
  new->node = node;
  new->stat_timestamp = 0;
  new->name_timestamp = 0;
  new->bulk_stat_base_stamp = 0;
  new->bulk_stat_count_first_half = 0;
  new->bulk_stat_count_second_half = 0;

  *dir = new;

  return 0;
}

void
ftpfs_dir_free (struct ftpfs_dir *dir)
{
  /* Free all entries.  */
  mark (dir);
  sweep (dir);

  hurd_ihash_destroy (&dir->htable);

  netfs_nrele (dir->node);

  free (dir);
}
