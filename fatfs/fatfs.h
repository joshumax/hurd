/* fatfs.h - Interface for fatfs.
   Copyright (C) 1997, 1999, 2002 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG and Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <sys/types.h>
#include <sys/mman.h>
#include <hurd/diskfs.h>
#include <hurd/diskfs-pager.h>
#include <hurd/store.h>

#include "fat.h"
#include "virt-inode.h"

/* There is no such thing as an inode in this format, all such information
   being recorded in the directory entry.  So we report inode numbers as
   the start cluster number of the file. When messing around with the
   directory entry, hold the DIRENT_LOCK.  */

struct disknode
{
  cluster_t start_cluster;

  /* Links on hash list.  */
  struct node *hnext, **hprevp;

  /* The inode as returned by virtual inode management routines.  */
  inode_t inode;

  struct rwlock dirent_lock;
    
  char *link_target;            /* For S_ISLNK.  */

  size_t translen;
  char *translator;

  /* Lock to hold while fiddling with this inode's block allocation
     info.  */
  struct rwlock alloc_lock;
  /* Lock to hold while extending this inode's block allocation info.
     Hold only if you hold readers alloc_lock, then you don't need to
     hold it if you hold writers alloc_lock already.  */
  spin_lock_t chain_extension_lock;
  struct cluster_chain *first;
  struct cluster_chain *last;
  cluster_t length_of_chain;
  int chain_complete;

  /* This file's pager.  */
  struct pager *pager;

  /* Index to start a directory lookup at.  */
  int dir_idx;
};

struct user_pager_info
{
  struct node *node;
  enum pager_type
  {
    FAT,
    FILE_DATA,
  } type;
  vm_prot_t max_prot;
};

/* The physical media.  */
extern struct store *store;

/* The UID and GID for all files in the filesystem.  */
extern uid_t fs_uid;
extern gid_t fs_gid;

/* Mapped image of the FAT.  */
extern void *fat_image;

/* Handy source of zeroes.  */
extern vm_address_t zerocluster;

extern struct dirrect dr_root_node;


#define round_cluster(offs)					\
  ((((offs) + bytes_per_cluster - 1)				\
    >> log2_bytes_per_cluster) << log2_bytes_per_cluster)

#define fat_first_cluster_byte(cluster) \
 (first_data_byte + ((cluster - 2) << log2_bytes_per_cluster))

void drop_pager_softrefs (struct node *);
void allow_pager_softrefs (struct node *);
void create_fat_pager (void);

void flush_node_pager (struct node *node);

void write_all_disknodes ();

struct node *ifind (ino_t inum);

error_t fat_get_next_cluster (cluster_t cluster, cluster_t *next_cluster);
void fat_to_unix_filename (const char *, char *);

error_t diskfs_cached_lookup_in_dirbuf (int cache_id, struct node **npp,
					vm_address_t buf);
void refresh_node_stats (void);

