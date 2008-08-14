/* procfs -- a translator for providing GNU/Linux compatible 
             proc pseudo-filesystem

   procfs.h -- This file is the main header file of this
               translator. This has important header 
               definitions for constants and functions 
               used in the translator.
               
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
   
   A portion of the code in this file is based on ftpfs code
   present in the hurd repositories copyrighted to FSF. The
   Copyright notice from that file is given below.
   
   Copyright (C) 1997,98,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
   This file is part of the GNU Hurd.
*/

#ifndef __PROCFS_H__
#define __PROCFS_H__

#define PROCFS_SERVER_NAME "procfs"
#define PROCFS_SERVER_VERSION "0.0.1"

/* /proc Filesystem type. */
#define PROCFILESYSTEM "procfs"

#define NUMBER_OF_FILES_PER_PID 1
#define JIFFY_ADJUST 100
#define PAGES_TO_BYTES(pages) ((pages) * sysconf(_SC_PAGESIZE))
#define BYTES_TO_PAGES(bytes) ((bytes) / sysconf(_SC_PAGESIZE))

#include <stdlib.h>
#include <unistd.h>
#include <cthreads.h>
#include <maptime.h>
#include <hurd/ihash.h>
#include <ps.h>

/* A single entry in a directory.  */
struct procfs_dir_entry
{
  char *name;			/* Name of this entry */
  size_t hv;			/* Hash value of NAME  */

  /* The active node referred to by this name (may be 0).
     NETFS_NODE_REFCNT_LOCK should be held while frobbing this.  */
  struct node *node;

  struct stat stat;
  char *symlink_target;
  time_t stat_timestamp;

  /* The directory to which this entry belongs.  */
  struct procfs_dir *dir;

  /* Link to next entry in hash bucket, and address of previous entry's (or
     hash table's) pointer to this entry.  If the SELF_P field is 0, then
     this is a deleted entry, awaiting final disposal.  */
  struct procfs_dir_entry *next, **self_p;

  /* Next entry in 'directory order', or 0 if none known.  */
  struct procfs_dir_entry *ordered_next, **ordered_self_p;

  /* When the presence/absence of this file was last checked.  */
  time_t name_timestamp;

  hurd_ihash_locp_t inode_locp;	/* Used for removing this entry */

  int noent : 1;		/* A negative lookup result.  */
  int valid : 1;		/* Marker for GC'ing.  */
};

/* A directory.  */
struct procfs_dir
{
  /* Number of entries in HTABLE.  */
  size_t num_entries;

  /* The number of entries that have nodes attached.  We keep an additional
     reference to our node if there are any, to prevent it from going away.  */
  size_t num_live_entries;

  /* Hash table of entries.  */
  struct procfs_dir_entry **htable;
  size_t htable_len;		/* # of elements in HTABLE (not bytes).  */

  /* List of dir entries in 'directory order', in a linked list using the
     ORDERED_NEXT and ORDERED_SELF_P fields in each entry.  Not all entries
     in HTABLE need be in this list.  */
  struct procfs_dir_entry *ordered;

  /* The filesystem node that this is the directory for.  */
  struct node *node;

  /* The filesystem this directory is in.  */
  struct procfs *fs;

  /* The path to this directory in the filesystem.  */
  const char *fs_path;

  time_t stat_timestamp;
  time_t name_timestamp;

};


/* libnetfs node structure */
struct netnode 
{ 
  /* Name of this node */
  char *name; 

  /* The path in the filesystem that corresponds
     this node. */
  char *fs_path;
    
  /* The directory entry for this node.  */
  struct procfs_dir_entry *dir_entry;
  
  /* The proc filesystem */
  struct procfs *fs;
  
  /* inode number, assigned to this netnode structure. */
  unsigned int inode_num;
  
  /* If this is a directory, the contents, or 0 if not fetched.  */
  struct procfs_dir *dir;
  
  /* pointer to node structure, assigned to this node. */
  struct node *node;
  
  /* links to the previous and next nodes in the list */
  struct netnode *nextnode, *prevnode;
  
  /* link to parent netnode of this file or directory */
  struct netnode *parent;
  
  /* link to the first child netnode of this directory */
  struct netnode *child_first;
};

/* The actual procfs filesystem structure */
struct procfs 
{
  /* Root of the filesystem. */
  struct node *root;
  
  /* Inode numbers are assigned sequentially in order of creation.  */
  ino_t next_inode;
  int fsid;
  
  /* A hash table mapping inode numbers to directory entries.  */
  struct hurd_ihash inode_mappings;
  spin_lock_t inode_mappings_lock;
};

extern struct procfs *procfs;

extern volatile struct mapped_time_value *procfs_maptime;

extern struct ps_context *ps_context;

/* Create a new procfs filesystem.  */
error_t procfs_create (char *procfs_root, int fsid,
                       struct procfs **fs);

/* Initialize the procfs filesystem for use. */
error_t procfs_init ();

/* Refresh stat information for NODE */
error_t procfs_refresh_node (struct node *node);

/* Return a new node in NODE, with a name NAME, 
   and return the new node with a single
   reference in NODE. */
error_t procfs_create_node (struct procfs_dir_entry *dir_entry, 
                            const char *fs_path,
                            struct node **node);

/* Remove NODE from its entry */
error_t procfs_remove_node (struct node *node);

/* Return in DIR a new procfs directory, in the filesystem FS,
   with node NODE and path PATH. */
error_t procfs_dir_create (struct procfs *fs, struct node *node,
			  const char *path, struct procfs_dir **dir);

/* Remove the specified DIR and free all its allocated
   storage. */
void procfs_dir_remove (struct procfs_dir *dir);

/* Refresh DIR.  */
error_t procfs_dir_refresh (struct procfs_dir *dir, int isroot);

/* Lookup NAME in DIR, returning its entry, or an error. 
   *NODE will contain the result node, locked, and with
   an additional reference, or 0 if an error occurs.  */
error_t procfs_dir_lookup (struct procfs_dir *dir, const char *name,
			  struct node **node);

#endif /* __PROCFS_H__ */
