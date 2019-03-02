/*
   Copyright (C) 2018 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <<a rel="nofollow" href="http://www.gnu.org/licenses/">http://www.gnu.org/licenses/</a>>.
*/

/* ACPI Filesystem header */

#ifndef ACPIFS_H
#define ACPIFS_H

#include <hurd/netfs.h>
#include <pthread.h>
#include <maptime.h>

#include <netfs_impl.h>
#include <acpi.h>

/* Size of a directory entry name */
#ifndef NAME_SIZE
#define NAME_SIZE 8
#endif

/* Node cache defaults size */
#define NODE_CACHE_MAX 16

/*
 * User and group ids to grant permission to acpi
 */
struct acpifs_perm
{
  int32_t uid;
  int32_t gid;
};

/*
 * Directory entry.
 *
 * All directory entries are created on startup and used to generate the
 * fs tree and create or retrieve libnetfs node objects.
 *
 * From libnetfs' point of view, these are the light nodes.
 */
struct acpifs_dirent
{
  char name[NAME_SIZE];
  struct acpifs_dirent *parent;
  io_statbuf_t stat;

  /*
   * We only need two kind of nodes: files and directories.
   * When `dir' is null, this is a file; when not null, it's a directory.
   */
  struct acpifs_dir *dir;

  /* Active node on this entry */
  struct node *node;

  /* ACPI table related to an entry */
  struct acpi_table *acpitable;
};

/*
 * A directory, it only contains a list of directory entries
 */
struct acpifs_dir
{
  /* Number of directory entries */
  uint16_t num_entries;

  /* Array of directory entries */
  struct acpifs_dirent **entries;
};

/* A particular ACPI filesystem.  */
struct acpifs
{
  /* Root of filesystem.  */
  struct node *root;

  /* A cache that holds a reference to recently used nodes.  */
  struct node *node_cache_mru, *node_cache_lru;
  size_t node_cache_len;       /* Number of entries in it.  */
  size_t node_cache_max;
  pthread_mutex_t node_cache_lock;

  struct acpifs_perm perm;

  struct acpifs_dirent *entries;
  size_t num_entries;
};

/* Main FS pointer */
struct acpifs *fs;

/* Global mapped time */
volatile struct mapped_time_value *acpifs_maptime;

/* Update entry and node times */
#define UPDATE_TIMES(e, what) (\
  {\
    fshelp_touch (&e->stat, what, acpifs_maptime);\
    if(e->node)\
      fshelp_touch (&e->node->nn_stat, what, acpifs_maptime);\
  }\
)

/* Update entry and node owner */
#define UPDATE_OWNER(e, uid) (\
  {\
    e->stat.st_uid = uid;\
    if(e->node)\
      e->node->nn_stat.st_uid = uid;\
  }\
)

/* Update entry and node group */
#define UPDATE_GROUP(e, gid) (\
  {\
    e->stat.st_gid = gid;\
    if(e->node)\
      e->node->nn_stat.st_gid = gid;\
  }\
)

/* FS manipulation functions */
error_t alloc_file_system (struct acpifs **fs);
error_t init_file_system (file_t underlying_node, struct acpifs *fs);
error_t create_fs_tree (struct acpifs *fs);
error_t fs_set_permissions (struct acpifs *fs);
error_t entry_check_perms (struct iouser *user, struct acpifs_dirent *e,
                          int flags);

#endif /* ACPIFS_H */
