/* Ftp filesystem

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

#ifndef __FTPFS_H__
#define __FTPFS_H__

#include <stdlib.h>
#include <pthread.h>
#include <ftpconn.h>
#include <maptime.h>
#include <hurd/ihash.h>

/* Anonymous types.  */
struct ccache;
struct ftpfs_conn;

/* A single entry in a directory.  */
struct ftpfs_dir_entry
{
  char *name;			/* Name of this entry */

  /* The active node referred to by this name (may be 0).
     NETFS_NODE_REFCNT_LOCK should be held while frobbing this.  */
  struct node *node;

  struct stat stat;
  char *symlink_target;
  time_t stat_timestamp;

  /* The directory to which this entry belongs.  */
  struct ftpfs_dir *dir;

  /* Next entry in `directory order', or 0 if none known.  */
  struct ftpfs_dir_entry *ordered_next, **ordered_self_p;

  /* When the presence/absence of this file was last checked.  */
  time_t name_timestamp;

  hurd_ihash_locp_t inode_locp;	/* Used for removing this entry */

  hurd_ihash_locp_t dir_locp; /* Position in the directory table.  */

  int noent : 1;		/* A negative lookup result.  */
  int valid : 1;		/* Marker for GC'ing.  */
  int deleted : 1;              /* Indicates a deleted entry.  */
};

/* A directory.  */
struct ftpfs_dir
{
  /* Hash table mapping names to children nodes.  */
  struct hurd_ihash htable;

  /* The number of entries that have nodes attached.  We keep an additional
     reference to our node if there are any, to prevent it from going away.  */
  size_t num_live_entries;

  /* List of dir entries in `directory order', in a linked list using the
     ORDERED_NEXT and ORDERED_SELF_P fields in each entry.  Not all entries
     in HTABLE need be in this list.  */
  struct ftpfs_dir_entry *ordered;

  /* The filesystem node that this is the directory for.  */
  struct node *node;

  /* The filesystem this directory is in.  */
  struct ftpfs *fs;

  /* The path to this directory on the server.  */
  const char *rmt_path;

  time_t stat_timestamp;
  time_t name_timestamp;

  /* Stuff for detecting bulk stats.  */

  /* The timestamp of the first sample in bulk_stat_count1, rounded to
     BULK_STAT_PERIOD seconds.  */
  time_t bulk_stat_base_stamp;

  /* The number of stats done in the period [bulk_stat_base_stamp,
     bulk_stat_base_stamp+BULK_STAT_PERIOD).  */
  unsigned bulk_stat_count_first_half;
  /* The number of stats done in the period
     [bulk_stat_base_stamp+BULK_STAT_PERIOD,
     bulk_stat_base_stamp+BULK_STAT_PERIOD*2).  */
  unsigned bulk_stat_count_second_half;
};

/* libnetfs node structure. */
struct netnode
{
  /* The remote filesystem.  */
  struct ftpfs *fs;

  /* The directory entry for this node.  */
  struct ftpfs_dir_entry *dir_entry;

  /* The path in FS that this file corresponds to.  */
  const char *rmt_path;

  /* If this is a regular file, an optional cache of the contents.  This may
     be 0, if no cache has yet been created, but once created, it only goes
     away when the node is destroyed.  */
  struct ccache *contents;

  /* If this is a directory, the contents, or 0 if not fetched.  */
  struct ftpfs_dir *dir;

  /* Position in the node cache.  */
  struct node *ncache_next, *ncache_prev;
};

/* Various parameters that can be used to change the behavior of an ftpfs.  */
struct ftpfs_params
{
  /* Amount of time name existence is cached.  */
  time_t name_timeout;

  /* Amount of time stat information is cached.  */
  time_t stat_timeout;

  /* Parameters for detecting bulk stats; if more than BULK_STAT_THRESHOLD
     stats are done within BULK_STAT_PERIOD seconds, the whole enclosing
     directory is fetched.  */
  time_t bulk_stat_period;
  unsigned bulk_stat_threshold;

  /* The size of the node cache.  */
  size_t node_cache_max;
};

/* A particular filesystem.  */
struct ftpfs 
{
  /* Root of filesystem.  */
  struct node *root;

  /* A pool of ftp connections for server threads to use.  */
  struct ftpfs_conn *free_conns;
  struct ftpfs_conn *conns;
  pthread_spinlock_t conn_lock;

  /* Parameters for making new ftp connections.  */
  struct ftp_conn_params *ftp_params;
  struct ftp_conn_hooks *ftp_hooks;

  /* Inode numbers are assigned sequentially in order of creation.  */
  ino_t next_inode;
  int fsid;

  /* A hash table mapping inode numbers to directory entries.  */
  struct hurd_ihash inode_mappings;
  pthread_spinlock_t inode_mappings_lock;

  struct ftpfs_params params;

  /* A cache that holds a reference to recently used nodes.  */
  struct node *node_cache_mru, *node_cache_lru;
  size_t node_cache_len;	/* Number of entries in it.  */
  pthread_mutex_t node_cache_lock;
};

extern volatile struct mapped_time_value *ftpfs_maptime;

/* The current time.  */
#define NOW \
  ({ struct timeval tv; maptime_read (ftpfs_maptime, &tv); tv.tv_sec; })

/* Create a new ftp filesystem with the given parameters.  */
error_t ftpfs_create (char *rmt_root, int fsid,
		      struct ftp_conn_params *ftp_params,
		      struct ftp_conn_hooks *ftp_hooks,
		      struct ftpfs_params *params,
		      struct ftpfs **fs);

/* Refresh stat information for NODE.  This may actually refresh the whole
   directory if that is deemed desirable.  */
error_t ftpfs_refresh_node (struct node *node);

/* Remove NODE from its entry (if the entry is still valid, it will remain
   without a node).  NODE should be locked.  */
error_t ftpfs_detach_node (struct node *node);

/* Return a new node in NODE, with a name NAME, and return the new node
   with a single reference in NODE.  E may be 0, if this is the root node.  */
error_t ftpfs_create_node (struct ftpfs_dir_entry *e, const char *rmt_path,
			   struct node **node);

/* Add NODE to the recently-used-node cache, which adds a reference to
   prevent it from going away.  NODE should be locked.  */
void ftpfs_cache_node (struct node *node);

/* Get an ftp connection to use for an operation. */
error_t ftpfs_get_ftp_conn (struct ftpfs *fs, struct ftp_conn **conn);

/* Return CONN to the pool of free connections in FS.  */
void ftpfs_release_ftp_conn (struct ftpfs *fs, struct ftp_conn *conn);

/* Return in DIR a new ftpfs directory, in the filesystem FS, with node NODE
   and remote path RMT_PATH.  RMT_PATH is *not copied*, so it shouldn't ever
   change while this directory is active.  */
error_t ftpfs_dir_create (struct ftpfs *fs, struct node *node,
			  const char *rmt_path, struct ftpfs_dir **dir);

void ftpfs_dir_free (struct ftpfs_dir *dir);

/* Refresh DIR.  */
error_t ftpfs_dir_refresh (struct ftpfs_dir *dir);

/* Lookup NAME in DIR, returning its entry, or an error.  DIR's node should
   be locked, and will be unlocked after returning; *NODE will contain the
   result node, locked, and with an additional reference, or 0 if an error
   occurs.  */
error_t ftpfs_dir_lookup (struct ftpfs_dir *dir, const char *name,
			  struct node **node);

/* Lookup the null name in DIR, and return a node for it in NODE.  Unlike
   ftpfs_dir_lookup, this won't attempt to validate the existence of the
   entry (to avoid opening a new connection if possible) -- that will happen
   the first time the entry is refreshed.  Also unlink ftpfs_dir_lookup, this
   function doesn't expect DIR to be locked, and won't return *NODE locked.
   This function is only used for bootstrapping the root node.  */
error_t ftpfs_dir_null_lookup (struct ftpfs_dir *dir, struct node **node);

#endif /* __FTPFS_H__ */
