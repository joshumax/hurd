/* Directory operations

   Copyright (C) 1997 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>
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

/* Return an alloca'd string containing NAME appended to DIR's path; if
   DIR_PFX_LEN is non-zero, the length of DIR's path is returned in it.  */
#define path_append(dir, name, dir_pfx_len)				      \
({									      \
   const char *_dname = (dir)->rmt_path;				      \
   const char *_name = (name);						      \
   size_t *_dir_pfx_len_p = (dir_pfx_len);				      \
   size_t _dir_pfx_len = strlen (_dname) + 1;				      \
   char *_path = alloca (_dir_pfx_len + strlen (_name) + 1);		      \
									      \
   /* Form the composite name.  */					      \
   if (_name && *_name)							      \
     if (_dname[0] == '/' && _dname[1] == '\0')				      \
       {								      \
	 stpcpy (stpcpy (_path, _dname), _name);		     	      \
	 _dir_pfx_len--;						      \
       }								      \
     else								      \
       stpcpy (stpcpy (stpcpy (_path, _dname), "/"), _name);		      \
   else									      \
     {									      \
       strcpy (_path, _dname);						      \
       _dir_pfx_len--;							      \
     }									      \
   if (_dir_pfx_len_p)							      \
     *_dir_pfx_len_p = _dir_pfx_len;					      \
									      \
   _path;								      \
})

/* Free the directory entry E and all resources it consumes.  */
void
free_entry (struct ftpfs_dir_entry *e)
{
  assert (! e->self_p);		/* We should only free deleted nodes.  */
  free (e->name);
  if (e->symlink_target)
    free (e->symlink_target);
  free (e);
}

/* Put the directory entry E into the hash table HTABLE, of length HTABLE_LEN.  */
static void
insert (struct ftpfs_dir_entry *e,
	struct ftpfs_dir_entry **htable, size_t htable_len)
{
  struct ftpfs_dir_entry **t = &htable[e->hv % htable_len];
  if (*t)
    (*t)->self_p = &e->next;
  e->next = *t;
  e->self_p = t;
  *t = e;
}

/* Replace DIR's hashtable with a new one of length NEW_LEN, retaining all
   existing entries.  */
static error_t
rehash (struct ftpfs_dir *dir, size_t new_len)
{
  int i;
  size_t old_len = dir->htable_len;
  struct ftpfs_dir_entry **old_htable = dir->htable;
  struct ftpfs_dir_entry **new_htable =
    malloc (new_len * sizeof (struct ftpfs_dir_entry *));

  if (! new_htable)
    return ENOMEM;

  for (i = 0; i < old_len; i++)
    while (old_htable[i])
      {
	struct ftpfs_dir_entry *e = old_htable[i];

	/* Remove E from the old table (don't bother to fixup
	   e->next->self_p).  */
	old_htable[i] = e->next;

	insert (e, new_htable, new_len);
      }

  free (old_htable);

  dir->htable = new_htable;
  dir->htable_len = new_len;

  return 0;
}

/* Calculate NAME's hash value.  */
static size_t
hash (const char *name)
{
  size_t hv = 0;
  while (*name)
    hv = ((hv << 5) + *name++) & 0xFFFFFF;
  return hv;
}

/* Lookup NAME in DIR and return its entry.  If there is no such entry, and
   ADD is true, then a new entry is allocated and returned, otherwise 0 is
   returned (if ADD is true then 0 can be returned if a memory allocation
   error occurs).  */
struct ftpfs_dir_entry *
lookup (struct ftpfs_dir *dir, const char *name, int add)
{
  size_t hv = hash (name);
  struct ftpfs_dir_entry *h = dir->htable[hv % dir->htable_len], *e = h;

  while (e && strcmp (name, e->name) != 0)
    e = e->next;

  if (!e && add)
    {
      e = malloc (sizeof *e);
      if (e)
	{
	  e->hv = hv;
	  e->name = strdup (name);
	  e->node = 0;
	  e->dir = dir;
	  e->stat_timestamp = 0;
	  bzero (&e->stat, sizeof e->stat);
	  e->symlink_target = 0;
	  e->noent = 0;
	  e->valid = 0;
	  e->name_timestamp = e->stat_timestamp = 0;
	  e->ordered_next = 0;
	  e->ordered_self_p = 0;
	  e->next = 0;
	  e->self_p = 0;
	  insert (e, dir->htable, dir->htable_len);
	  dir->num_entries++;
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
    e->ordered_next->self_p = e->ordered_self_p;
}

/* Delete E from its directory, freeing any resources it holds.  */
static void
delete (struct ftpfs_dir_entry *e, struct ftpfs_dir *dir)
{
  dir->num_entries--;

  /* Take out of the hash chain.  */
  if (e->self_p)
    *e->self_p = e->next;
  if (e->next)
    e->next->self_p = e->self_p;

  /* This indicates a deleted entry.  */
  e->self_p = 0;
  e->next = 0;

  /* Take out of the directory ordered list.  */
  ordered_unlink (e);

  /* Now stick in the deleted list.  */
}

/* Clear the valid bit in all DIR's htable.  */
static void
mark (struct ftpfs_dir *dir)
{
  size_t len = dir->htable_len, i;
  struct ftpfs_dir_entry **htable = dir->htable, *e;

  for (i = 0; i < len; i++)
    for (e = htable[i]; e; e = e->next)
      e->valid = 0;
}

/* Delete any entries in DIR which don't have their valid bit set.  */
static void
sweep (struct ftpfs_dir *dir)
{
  size_t len = dir->htable_len, i;
  struct ftpfs_dir_entry **htable = dir->htable, *e;

  for (i = 0; i < len; i++)
    for (e = htable[i]; e; e = e->next)
      if (! e->valid)
	delete (e, dir);
}

/* Inode numbers.  */
ino_t ftpfs_next_inode = 2;

/* Update the directory entry for NAME to reflect ST and SYMLINK_TARGET.
   True is returned if successful, or false if there was a memory allocation
   error.  TIMESTAMP is used to record the time of this update.  */
static void
update_entry (struct ftpfs_dir_entry *e, const struct stat *st,
	      const char *symlink_target, time_t timestamp)
{
  ino_t ino;

  if (e->stat.st_ino)
    ino = e->stat.st_ino;
  else
    ino = ftpfs_next_inode++;

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
  struct ftpfs_dir_entry *prev_entry;
  time_t timestamp;
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
  struct ftpfs_dir_entry *e = lookup (dfs->dir, name, 1), *pe;

  if (! e)
    return ENOMEM;

  update_entry (e, st, symlink_target, dfs->timestamp);
  e->valid = 1;

  assert (! e->ordered_self_p);
  assert (! e->ordered_next);

  /* Position E in the ordered chain.  */
  pe = dfs->prev_entry;		/* Previously seen entry.  */
  if (pe)
    e->ordered_self_p = &pe->ordered_next; /* Put E after PE.  */
  else
    e->ordered_self_p = &dfs->dir->ordered; /* Put E at beginning.  */
  assert (! *e->ordered_self_p);/* There shouldn't be anything in E's place. */

  *e->ordered_self_p = e;	/* Put E there.  */
  dfs->prev_entry = e;		/* Put the next entry after this one.  */

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
   updated.  */
static error_t
refresh_dir (struct ftpfs_dir *dir, int update_stats, time_t timestamp)
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

  /* Clear DIR's ordered entry list.  */
  if (dir->ordered)
    {
      struct ftpfs_dir_entry *e, *next;
      for (e = dir->ordered; e; e = next)
	{
	  next = e->ordered_next;
	  e->ordered_next = 0;
	  e->ordered_self_p = 0;
	}	
      dir->ordered = 0;
    }

  /* Mark directory entries so we can GC them later using sweep.  */
  mark (dir);

  reset_bulk_stat_info (dir);

  /* Info passed to update_ordered_entry.  */
  dfs.dir = dir;
  dfs.prev_entry = 0;
  dfs.timestamp = timestamp;

  /* Refetch the directory from the server.  */
  if (update_stats)
    /* Fetch both names and stat info.  */
    err = ftp_conn_get_stats (conn, dir->rmt_path, 1,
			      update_ordered_entry, &dfs);
  else
    /* Just fetch names.  */
    err = ftp_conn_get_names (conn, dir->rmt_path, update_ordered_name, &dfs);
  
  if (! err)
    /* GC any directory entries that weren't seen this time.  */
    {
      dir->name_timestamp = timestamp;
      if (update_stats)
	dir->stat_timestamp = timestamp;
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
  return refresh_dir (dir, 0, timestamp);
}

/* State shared between ftpfs_dir_entry_refresh and update_old_entry.  */
struct refresh_entry_state
{
  struct ftpfs_dir_entry *entry;
  time_t timestamp;
  /* Prefix to skip at beginning of name returned from listing.  */
  const char *dir_pfx;
  size_t dir_pfx_len;
};

/* Update the directory entry for NAME to reflect ST and SYMLINK_TARGET.
   HOOK points to the state from ftpfs_dir_fetch_entry.  */
static error_t
update_old_entry (const char *name, const struct stat *st,
		  const char *symlink_target, void *hook)
{
  struct refresh_entry_state *res = hook;

  /* Skip the directory part of the name.  */
  if (strncmp (name, res->dir_pfx, res->dir_pfx_len) != 0)
    return EGRATUITOUS;
  else
    name += res->dir_pfx_len;

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

      mutex_lock (&dir->node->lock);

      if (! entry->self_p)
	/* This is a deleted entry, just awaiting disposal; do so.  */
	{
	  nn->dir_entry = 0;
	  free_entry (entry);
	  return 0;
	}
      else if (entry->stat_timestamp + dir->fs->params.stat_timeout < timestamp)
	/* Stat information needs updating.  */
	if (need_bulk_stat (timestamp, dir))
	  /* Refetch the whole directory from the server.  */
	  err =  refresh_dir (entry->dir, 1, timestamp);
	else
	  {
	    struct ftp_conn *conn;
	    struct refresh_entry_state res;
	    const char *path = path_append (dir, entry->name, &res.dir_pfx_len);

	    res.entry = entry;
	    res.timestamp = timestamp;
	    res.dir_pfx = path;

	    err = ftpfs_get_ftp_conn (dir->fs, &conn);
	    if (! err)
	      {
		err =
		  ftp_conn_get_stats (conn, path, 0, update_old_entry, &res);
		ftpfs_release_ftp_conn (dir->fs, conn);
	      }
	  }

      if ((entry->stat.st_mtime < node->nn_stat.st_mtime
	   || entry->stat.st_size != node->nn_stat.st_size)
	  && nn && nn->contents)
	/* The file has changed.  */
	ccache_invalidate (nn->contents);

      node->nn_stat = entry->stat;
      if (!nn->dir && S_ISDIR (entry->stat.st_mode))
	ftpfs_dir_create (nn->fs, node, nn->rmt_path, &nn->dir);

      mutex_unlock (&dir->node->lock);

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

      mutex_lock (&dir->node->lock);

      if (entry->self_p)
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
	mutex_unlock (&dir->node->lock);
    }

  return 0;
}

/* State shared between ftpfs_dir_lookup and update_new_entry.  */
struct new_entry_state
{
  time_t timestamp;
  struct ftpfs_dir *dir;
  struct ftpfs_dir_entry *entry;
  /* Prefix to skip at beginning of name returned from listing.  */
  const char *dir_pfx;
  size_t dir_pfx_len;
};

/* Update the directory entry for NAME to reflect ST and SYMLINK_TARGET.
   HOOK->entry will be updated to reflect the new entry.  */
static error_t
update_new_entry (const char *name, const struct stat *st,
		  const char *symlink_target, void *hook)
{
  struct ftpfs_dir_entry *e;
  struct new_entry_state *nes = hook;

  /* Skip the directory part of the name.  */
  if (strncmp (name, nes->dir_pfx, nes->dir_pfx_len) != 0)
    return EGRATUITOUS;
  else
    name += nes->dir_pfx_len;

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
  error_t err = 0;
  time_t timestamp = NOW;
  struct ftpfs_dir_entry *e;

  if (strcmp (name, ".") == 0)
    {
      netfs_nref (dir->node);
      *node = dir->node;
      return 0;
    }
  else if (strcmp (name, "..") == 0)
    {
      if (dir->node->nn->dir_entry)
	{
	  *node = dir->node->nn->dir_entry->dir->node;
	  mutex_lock (&(*node)->lock);
	  netfs_nref (*node);
	}
      else
	{
	  err = ENOENT;		/* No .. */
	  *node = 0;
	}

      mutex_unlock (&dir->node->lock);

      return err;
    }

  e = lookup (dir, name, 0);
  if (!e || e->name_timestamp + dir->fs->params.name_timeout < timestamp)
    /* Try to fetch info about NAME.  */
    {
      if (need_bulk_stat (timestamp, dir))
	/* Refetch the whole directory from the server.  */
	{
	  err =  refresh_dir (dir, 1, timestamp);
	  if (! err)
	    e = lookup (dir, name, 0);
	}
      else
	{
	  struct ftp_conn *conn;

	  err = ftpfs_get_ftp_conn (dir->fs, &conn);
	  if (! err)
	    {
	      struct new_entry_state nes;
	      const char *path = path_append (dir, name, &nes.dir_pfx_len);

	      nes.dir = dir;
	      nes.timestamp = timestamp;
	      nes.dir_pfx = path;

	      err = ftp_conn_get_stats (conn, path, 0, update_new_entry, &nes);
	      if (! err)
		e = nes.entry;
	      else if (err == ENOENT)
		{
		  e = lookup (dir, name, 1);
		  if (! e)
		    err = ENOMEM;
		  else
		    e->noent = 1;	/* A negative entry.  */
		}

	      ftpfs_release_ftp_conn (dir->fs, conn);
	    }
	}
    }

  if (! err)
    if (e && !e->noent)
      /* We've got a dir entry, get a node for it.  */
      {
	/* If there's already a node, add a ref so that it doesn't go away.  */
	spin_lock (&netfs_node_refcnt_lock);
	if (e->node)
	  e->node->references++;
	spin_unlock (&netfs_node_refcnt_lock);

	if (! e->node)
	  /* No node; make one and install it into E.  */
	  {
	    const char *path = path_append (dir, name, 0);
	    err = ftpfs_create_node (e, path, &e->node);
	    if (!err && dir->num_live_entries++ == 0)
	      /* Keep a reference to dir's node corresponding to children.  */
	      {
		spin_lock (&netfs_node_refcnt_lock);
		dir->node->references++;
		spin_unlock (&netfs_node_refcnt_lock);
	      }
	  }

	if (! err)
	  {
	    *node = e->node;
	    mutex_unlock (&dir->node->lock);
	    mutex_lock (&e->node->lock);
	  }
      }
    else
      err = ENOENT;

  if (err)
    {
      *node = 0;
      mutex_unlock (&dir->node->lock);
    }

  return err;
}

/* Return in DIR a new ftpfs directory, in the filesystem FS, with node NODE
   and remote path RMT_PATH.  RMT_PATH is *not copied*, so it shouldn't ever
   change while this directory is active.  */
error_t
ftpfs_dir_create (struct ftpfs *fs, struct node *node, const char *rmt_path,
		  struct ftpfs_dir **dir)
{
  struct ftpfs_dir *new = malloc (sizeof (struct ftpfs_dir));

  if (! new)
    return ENOMEM;

  /* Hold a reference to the new dir's node.  */
  spin_lock (&netfs_node_refcnt_lock);
  node->references++;
  spin_unlock (&netfs_node_refcnt_lock);

  new->num_entries = 0;
  new->num_live_entries = 0;
  new->htable_len = 5;
  new->htable = malloc (new->htable_len * sizeof (struct ftpfs_dir_entry *));
  bzero (new->htable, sizeof *new->htable * new->htable_len);
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

  if (dir->htable)
    free (dir->htable);

  netfs_nrele (dir->node);

  free (dir);
}
