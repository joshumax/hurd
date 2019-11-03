/*
   Copyright (C) 2017 Free Software Foundation, Inc.
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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

/* PCI Filesystem header */

#ifndef PCIFS_H
#define PCIFS_H

#include <hurd/netfs.h>
#include <pthread.h>
#include <maptime.h>

#include <pciaccess.h>
// FIXME: Hardcoded PCI config size
#define PCI_CONFIG_SIZE 256

#include <netfs_impl.h>

/* Size of a directory entry name */
#ifndef NAME_SIZE
#define NAME_SIZE 16
#endif

/* Node cache defaults size */
#define NODE_CACHE_MAX 16

/*
 * Directory entry. Contains all per-node data our problem requires.
 *
 * All directory entries are created on startup and used to generate the
 * fs tree and create or retrieve libnetfs node objects.
 *
 * From libnetfs' point of view, these are the light nodes.
 */
struct pcifs_dirent
{
  /*
   * Complete bus identification, including domain, of the device.  On
   * platforms that do not support PCI domains (e.g., 32-bit x86 hardware),
   * the domain will always be zero.
   *
   * Negative value means no value.
   */
  int32_t domain;
  int16_t bus;
  int16_t dev;
  int8_t func;

  /*
   * Device's class, subclass, and programming interface packed into a
   * single 32-bit value.  The class is at bits [23:16], subclass is at
   * bits [15:8], and programming interface is at [7:0].
   *
   * Negative value means no value.
   */
  int32_t device_class;

  char name[NAME_SIZE];
  struct pcifs_dirent *parent;
  io_statbuf_t stat;

  /*
   * We only need two kind of nodes: files and directories.
   * When `dir' is null, this is a file; when not null, it's a directory.
   */
  struct pcifs_dir *dir;

  /* Active node on this entry */
  struct node *node;

  /*
   * Underlying virtual device if any.
   *
   * Only for entries having a full B/D/F address.
   */
  struct pci_device *device;
};

/*
 * A directory, it only contains a list of directory entries
 */
struct pcifs_dir
{
  /* Number of directory entries */
  uint16_t num_entries;

  /* Array of directory entries */
  struct pcifs_dirent **entries;
};

/*
 * Set of permissions.
 *
 * For each Class[,subclass] and/or Domain[,bus[,dev[,func]]], one UID and/or GID.
 */
struct pcifs_perm
{
  /*
   * D/b/d/f scope of permissions.
   *
   * Negative value means no value.
   */
  int32_t domain;
  int16_t bus;
  int16_t dev;
  int8_t func;

  /*
   * Class and subclass scope of permissions
   *
   * Negative value means no value.
   */
  int16_t d_class;
  int16_t d_subclass;

  /* User and group ids */
  int32_t uid;
  int32_t gid;
};

/* Various parameters that can be used to change the behavior of an ftpfs.  */
struct pcifs_params
{
  /* The size of the node cache.  */
  size_t node_cache_max;

  /* FS permissions.  */
  struct pcifs_perm *perms;
  size_t num_perms;
};

/* A particular filesystem.  */
struct pcifs
{
  /* Root of filesystem.  */
  struct node *root;

  /* FS configuration */
  struct pcifs_params params;

  /* A cache that holds a reference to recently used nodes.  */
  struct node *node_cache_mru, *node_cache_lru;
  size_t node_cache_len;	/* Number of entries in it.  */
  pthread_mutex_t node_cache_lock;

  /* Lock for pci_conf operations */
  pthread_mutex_t pci_conf_lock;

  struct pcifs_dirent *entries;
  size_t num_entries;
};

/* Main FS pointer */
struct pcifs *fs;

/* Global mapped time */
volatile struct mapped_time_value *pcifs_maptime;

/* Update entry and node times */
#define UPDATE_TIMES(e, what) (\
  {\
    fshelp_touch (&e->stat, what, pcifs_maptime);\
    if(e->node)\
      fshelp_touch (&e->node->nn_stat, what, pcifs_maptime);\
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
error_t alloc_file_system (struct pcifs **fs);
error_t init_file_system (file_t underlying_node, struct pcifs *fs);
error_t create_fs_tree (struct pcifs *fs);
error_t fs_set_permissions (struct pcifs *fs);
error_t entry_check_perms (struct iouser *user, struct pcifs_dirent *e,
                          int flags);

#endif /* PCIFS_H */
