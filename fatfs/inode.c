/* inode.c - Inode management routines.
   Copyright (C) 1994,95,96,97,98,99,2000,02,03 Free Software Foundation, Inc.
   Modified for fatfs by Marcus Brinkmann <marcus@gnu.org>

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

#include <string.h>
#include "fatfs.h"

/* These flags aren't actually defined by a header file yet, so
   temporarily disable them if necessary.  */
#ifndef UF_APPEND
#define UF_APPEND 0
#endif
#ifndef UF_NODUMP
#define UF_NODUMP 0
#endif
#ifndef UF_IMMUTABLE
#define UF_IMMUTABLE 0
#endif

#define INOHSZ  512
#if     ((INOHSZ&(INOHSZ-1)) == 0)
#define INOHASH(ino)    ((ino)&(INOHSZ-1))
#else
#define INOHASH(ino)    (((unsigned)(ino))%INOHSZ)
#endif

static struct node *nodehash[INOHSZ];

static error_t read_node (struct node *np, vm_address_t buf);

/* Initialize the inode hash table.  */
void
inode_init ()
{
  int n;
  for (n = 0; n < INOHSZ; n++)
    nodehash[n] = 0;
}

/* Fetch inode INUM, set *NPP to the node structure; gain one user
   reference and lock the node.  */
error_t
diskfs_cached_lookup (ino64_t inum, struct node **npp)
{
  error_t err;
  struct node *np;
  struct disknode *dn;

  spin_lock (&diskfs_node_refcnt_lock);
  for (np = nodehash[INOHASH(inum)]; np; np = np->dn->hnext)
    if (np->cache_id == inum)
      {
        np->references++;
        spin_unlock (&diskfs_node_refcnt_lock);
        mutex_lock (&np->lock);
        *npp = np;
        return 0;
      }

  /* Format specific data for the new node.  */
  dn = malloc (sizeof (struct disknode));
  if (! dn)
    {
      spin_unlock (&diskfs_node_refcnt_lock);
      return ENOMEM;
    }
  dn->pager = 0;
  dn->first = 0;
  dn->last = 0;
  dn->length_of_chain = 0;
  dn->chain_complete = 0;
  dn->chain_extension_lock = SPIN_LOCK_INITIALIZER;
  rwlock_init (&dn->alloc_lock);
  rwlock_init (&dn->dirent_lock);
  
  /* Create the new node.  */
  np = diskfs_make_node (dn);
  np->cache_id = inum;
  np->dn->inode = vi_lookup(inum);

  mutex_lock (&np->lock);
  
  /* Put NP in NODEHASH.  */
  dn->hnext = nodehash[INOHASH(inum)];
  if (dn->hnext)
    dn->hnext->dn->hprevp = &dn->hnext;
  dn->hprevp = &nodehash[INOHASH(inum)];
  nodehash[INOHASH(inum)] = np;

  spin_unlock (&diskfs_node_refcnt_lock);
  
  /* Get the contents of NP off disk.  */
  err = read_node (np, 0);

  if (err)
    return err;
  else
    {
      *npp = np;
      return 0;
    }
}

/* Fetch inode INUM, set *NPP to the node structure;
   gain one user reference and lock the node.
   On the way, use BUF as the directory file map.  */
error_t
diskfs_cached_lookup_in_dirbuf (int inum, struct node **npp, vm_address_t buf)
{
  error_t err;
  struct node *np;
  struct disknode *dn;

  spin_lock (&diskfs_node_refcnt_lock);
  for (np = nodehash[INOHASH(inum)]; np; np = np->dn->hnext)
    if (np->cache_id == inum)
      {
        np->references++;
        spin_unlock (&diskfs_node_refcnt_lock);
        mutex_lock (&np->lock);
        *npp = np;
        return 0;
      }

  /* Format specific data for the new node.  */
  dn = malloc (sizeof (struct disknode));
  if (! dn)
    {
      spin_unlock (&diskfs_node_refcnt_lock);
      return ENOMEM;
    }
  dn->pager = 0;
  dn->first = 0;
  dn->last = 0;
  dn->length_of_chain = 0;
  dn->chain_complete = 0;
  dn->chain_extension_lock = SPIN_LOCK_INITIALIZER;
  rwlock_init (&dn->alloc_lock);
  rwlock_init (&dn->dirent_lock);
  
  /* Create the new node.  */
  np = diskfs_make_node (dn);
  np->cache_id = inum;
  np->dn->inode = vi_lookup(inum);

  mutex_lock (&np->lock);
  
  /* Put NP in NODEHASH.  */
  dn->hnext = nodehash[INOHASH(inum)];
  if (dn->hnext)
    dn->hnext->dn->hprevp = &dn->hnext;
  dn->hprevp = &nodehash[INOHASH(inum)];
  nodehash[INOHASH(inum)] = np;

  spin_unlock (&diskfs_node_refcnt_lock);
  
  /* Get the contents of NP off disk.  */
  err = read_node (np, buf);

  if (err)
    return err;
  else
    {
      *npp = np;
      return 0;
    }
}

/* Lookup node INUM (which must have a reference already) and return
   it without allocating any new references.  */
struct node *
ifind (ino_t inum)
{
  struct node *np;

  spin_lock (&diskfs_node_refcnt_lock);
  for (np = nodehash[INOHASH(inum)]; np; np = np->dn->hnext)
    {
      if (np->cache_id != inum)
        continue;

      assert (np->references);
      spin_unlock (&diskfs_node_refcnt_lock);
      return np;
    }
  assert (0);
}

/* The last reference to a node has gone away; drop it from the hash
   table and clean all state in the dn structure.  */
void
diskfs_node_norefs (struct node *np)
{
  struct cluster_chain *last = np->dn->first;

  *np->dn->hprevp = np->dn->hnext;
  if (np->dn->hnext)
    np->dn->hnext->dn->hprevp = np->dn->hprevp;
  
  while (last)
    {
      struct cluster_chain *next = last->next;
      free(last);
      last = next;
    }

  if (np->dn->translator)
    free (np->dn->translator);

  assert (!np->dn->pager);

  free (np->dn);
  free (np);
}

/* The last hard reference to a node has gone away; arrange to have
   all the weak references dropped that can be.  */
void
diskfs_try_dropping_softrefs (struct node *np)
{
  drop_pager_softrefs (np);
}

/* The last hard reference to a node has gone away.  */
void
diskfs_lost_hardrefs (struct node *np)
{
}

/* A new hard reference to a node has been created; it's now OK to
   have unused weak references. */
void
diskfs_new_hardrefs (struct node *np)
{
  allow_pager_softrefs (np);
}

/* Read stat information out of the directory entry. */
static error_t
read_node (struct node *np, vm_address_t buf)
{
  /* XXX This needs careful investigation */
  error_t err;
  struct stat *st = &np->dn_stat;
  struct disknode *dn = np->dn;
  struct dirrect *dr;
  struct node *dp = 0;
  struct vi_key vk = vi_key(np->dn->inode);
  vm_prot_t prot = VM_PROT_READ;
  memory_object_t memobj;
  vm_size_t buflen = 0;
  int our_buf = 0;

  if (vk.dir_inode == 0)
    dr = &dr_root_node;
  else
    {
      if (buf == 0)
	{
	  /* FIXME: We know intimately that the parent dir is locked
	     by libdiskfs.  The only case it is not locked is for NFS
	     (fsys_getfile) and we disabled that.  */
	  dp = ifind (vk.dir_inode);
      
	  /* Map in the directory contents. */
	  memobj = diskfs_get_filemap (dp, prot);
      
	  if (memobj == MACH_PORT_NULL)
	    return errno;

	  buflen = round_page (dp->dn_stat.st_size);
	  err = vm_map (mach_task_self (),
			&buf, buflen, 0, 1, memobj, 0, 0, prot, prot, 0);
	  mach_port_deallocate (mach_task_self (), memobj);
	  our_buf = 1;
	}
      
      dr = (struct dirrect *) (buf + vk.dir_offset);
    }

  st->st_fstype = FSTYPE_MSLOSS;
  st->st_fsid = getpid ();
  st->st_ino = np->cache_id;
  st->st_gen = 0;
  st->st_rdev = 0;

  st->st_nlink = 1;
  st->st_uid = fs_uid;
  st->st_gid = fs_gid;

  st->st_rdev = 0;

  np->dn->translator = 0;
  np->dn->translen = 0;

  st->st_flags = 0;

  /* If we are called for a newly allocated node that has no directory
     entry yet, only set a minimal amount of data until the dirent is
     created (and we get called a second time?).  */
  /* We will avoid this by overriding the relevant functions.
     if (dr == (void *)1)
     return 0;
  */

  rwlock_reader_lock(&np->dn->dirent_lock);

  dn->start_cluster = (read_word (dr->first_cluster_high) << 16)
    + read_word (dr->first_cluster_low);

  if (dr->attribute & FAT_DIR_ATTR_DIR)
    {
      st->st_mode = S_IFDIR | 0777;
      /* When we read in the node the first time, diskfs_root_node is
	 zero.  */
      if ((diskfs_root_node == 0 || np == diskfs_root_node) &&
          (fat_type = FAT12 || fat_type == FAT16))
	{
	  st->st_size = read_dword (dr->file_size);
	  np->allocsize = nr_of_root_dir_sectors << log2_bytes_per_sector;
	}
      else
	{
	  np->allocsize = 0;
	  rwlock_reader_lock(&dn->alloc_lock);
	  err = fat_extend_chain (np, FAT_EOC, 0);
	  rwlock_reader_unlock(&dn->alloc_lock);
	  if (err)
	    {
	      if (our_buf && buf)
		munmap ((caddr_t) buf, buflen);
	      return err;
	    }
	  st->st_size = np->allocsize;
	}
    }
  else
    {
      unsigned offset;
      st->st_mode = S_IFREG | 0666;
      st->st_size = read_dword (dr->file_size);
      np->allocsize = np->dn_stat.st_size;

      /* Round up to a cluster multiple.  */
      offset = np->allocsize & (bytes_per_cluster - 1);
      if (offset > 0)
	np->allocsize += bytes_per_cluster - offset;
    }
  if (dr->attribute & FAT_DIR_ATTR_RDONLY)
    st->st_mode &= ~0222;

  {
    struct timespec ts;
    fat_to_epoch (dr->write_date, dr->write_time, &ts);
    st->st_ctime = st->st_mtime = st->st_atime = ts.tv_sec;
    st->st_ctime_usec = st->st_mtime_usec = st->st_atime_usec
      = ts.tv_nsec * 1000;
  }
  
  st->st_blksize = bytes_per_sector;
  st->st_blocks = (st->st_size - 1) / bytes_per_sector + 1;

  rwlock_reader_unlock(&np->dn->dirent_lock);

  if (our_buf && buf)
    munmap ((caddr_t) buf, buflen);
  return 0;
}

/* Return 0 if NP's owner can be changed to UID; otherwise return an
   error code.  */
error_t
diskfs_validate_owner_change (struct node *np, uid_t uid)
{
  /* Allow configurable uid. */
  if (uid != 0)
    return EINVAL;
  return 0;
}

/* Return 0 if NP's group can be changed to GID; otherwise return an
   error code.  */
error_t
diskfs_validate_group_change (struct node *np, gid_t gid)
{
  /* Allow configurable gid. */
  if (gid != 0)
    return EINVAL;
  return 0;
}

/* Return 0 if NP's mode can be changed to MODE; otherwise return an
   error code.  It must always be possible to clear the mode; diskfs
   will not ask for permission before doing so.  */
error_t
diskfs_validate_mode_change (struct node *np, mode_t mode)
{
  /* XXX */
  return 0;
}

/* Return 0 if NP's author can be changed to AUTHOR; otherwise return
   an error code.  */
error_t
diskfs_validate_author_change (struct node *np, uid_t author)
{
  return (author == np->dn_stat.st_uid) ? 0 : EINVAL;
}

/* The user may define this function.  Return 0 if NP's flags can be
   changed to FLAGS; otherwise return an error code.  It must always
   be possible to clear the flags.  */
error_t
diskfs_validate_flags_change (struct node *np, int flags)
{
  if (flags & ~(UF_NODUMP | UF_IMMUTABLE | UF_APPEND))
    return EINVAL;
  else
    return 0;
}

/* Writes everything from NP's inode to the disk image.  */
void
write_node (struct node *np)
{
  error_t err;
  struct stat *st = &np->dn_stat;
  struct dirrect *dr;
  struct node *dp;
  struct vi_key vk = vi_key(np->dn->inode);
  vm_prot_t prot = VM_PROT_READ | VM_PROT_WRITE;
  memory_object_t memobj;
  vm_address_t buf = 0;
  vm_size_t buflen;

  /* XXX: If we are called from node-create before direnter was
     called, DR is zero and we can't update the node. Just return
     here, and leave it to direnter to call us again when we are
     ready.
     If we are called for the root directory node, we can't do anything,
     as FAT root dirs don't have a directory entry for themselve.
  */
  if (vk.dir_inode == 0 || np == diskfs_root_node)
    return;

  assert (!np->dn_set_ctime && !np->dn_set_atime && !np->dn_set_mtime);
  if (np->dn_stat_dirty)
    {
      assert (!diskfs_readonly);

      err = diskfs_cached_lookup (vk.dir_inode, &dp);
      if (err)
	return;

      /* Map in the directory contents. */
      memobj = diskfs_get_filemap (dp, prot);

      if (memobj == MACH_PORT_NULL)
	return;

      buflen = round_page (dp->dn_stat.st_size);
      err = vm_map (mach_task_self (),
		    &buf, buflen, 0, 1, memobj, 0, 0, prot, prot, 0);
      mach_port_deallocate (mach_task_self (), memobj);

      dr = (struct dirrect *) (buf + vk.dir_offset);

      rwlock_writer_lock(&np->dn->dirent_lock);
      write_word (dr->first_cluster_low, np->dn->start_cluster & 0xffff);
      write_word (dr->first_cluster_high, np->dn->start_cluster >> 16);

      write_dword (dr->file_size, st->st_size);

      /* Write time. */
      fat_from_epoch ((unsigned char *) &dr->write_date,
		      (unsigned char *) &dr->write_time, &st->st_mtime);

      rwlock_writer_unlock(&np->dn->dirent_lock);
      np->dn_stat_dirty = 0;

      munmap ((caddr_t) buf, buflen);
      diskfs_nput (dp);
    }
}

/* Reload all data specific to NODE from disk, without writing anything.
   Always called with DISKFS_READONLY true.  */
error_t
diskfs_node_reload (struct node *node)
{
  struct cluster_chain *last = node->dn->first;

  while (last)
    {
      struct cluster_chain *next = last->next;
      free(last);
      last = next;
    }
  flush_node_pager (node);
  read_node (node, 0);

  return 0;
}

/* For each active node, call FUN.  The node is to be locked around the call
   to FUN.  If FUN returns non-zero for any node, then immediately stop, and
   return that value.  */
error_t
diskfs_node_iterate (error_t (*fun)(struct node *))
{
  error_t err = 0;
  int n, num_nodes = 0;
  struct node *node, **node_list, **p;

  spin_lock (&diskfs_node_refcnt_lock);

  /* We must copy everything from the hash table into another data structure
     to avoid running into any problems with the hash-table being modified
     during processing (normally we delegate access to hash-table with
     diskfs_node_refcnt_lock, but we can't hold this while locking the
     individual node locks).  */

  for (n = 0; n < INOHSZ; n++)
    for (node = nodehash[n]; node; node = node->dn->hnext)
      num_nodes++;

  node_list = alloca (num_nodes * sizeof (struct node *));
  p = node_list;
  for (n = 0; n < INOHSZ; n++)
    for (node = nodehash[n]; node; node = node->dn->hnext)
      {
        *p++ = node;
        node->references++;
      }

  spin_unlock (&diskfs_node_refcnt_lock);

  p = node_list;
  while (num_nodes-- > 0)
    {
      node = *p++;
      if (!err)
        {
          mutex_lock (&node->lock);
          err = (*fun)(node);
          mutex_unlock (&node->lock);
        }
      diskfs_nrele (node);
    }

  return err;
}

/* Write all active disknodes into the ext2_inode pager. */
void
write_all_disknodes ()
{
  error_t write_one_disknode (struct node *node)
    {
      diskfs_set_node_times (node);

      /* Update the inode image.  */
      write_node (node);

      return 0;
    }
  
  diskfs_node_iterate (write_one_disknode);
}


void
refresh_node_stats ()
{
  error_t refresh_one_node_stat (struct node *node)
    {
      node->dn_stat.st_uid = fs_uid;
      node->dn_stat.st_gid = fs_gid;
      return 0;
    }

  diskfs_node_iterate (refresh_one_node_stat);
}


/* Sync the info in NP->dn_stat and any associated format-specific
   information to disk.  If WAIT is true, then return only after the
   physicial media has been completely updated.  */
void
diskfs_write_disknode (struct node *np, int wait)
{
  write_node (np);
}

/* Set *ST with appropriate values to reflect the current state of the
   filesystem.  */
error_t
diskfs_set_statfs (struct statfs *st)
{
  st->f_type = FSTYPE_MSLOSS;
  st->f_bsize = bytes_per_sector;
  st->f_blocks = total_sectors;
  st->f_bfree = fat_get_freespace () * sectors_per_cluster;
  st->f_bavail = st->f_bfree;
  /* There is no easy way to determine the number of (free) files on a
     FAT filesystem.  */
  st->f_files = 0;
  st->f_ffree = 0;
  st->f_fsid = getpid ();
  st->f_namelen = 0;
  st->f_favail = st->f_ffree;
  st->f_frsize = bytes_per_cluster;
  return 0;
}

error_t
diskfs_set_translator (struct node *node,
		       const char *name, u_int namelen,
		       struct protid *cred)
{
  assert (!diskfs_readonly);
  return EOPNOTSUPP;
}

error_t
diskfs_get_translator (struct node *node, char **namep, u_int *namelen)
{
  assert(0);
}

void
diskfs_shutdown_soft_ports ()
{
    /* Should initiate termination of internally held pager ports
     (the only things that should be soft) XXX */
}

/* The user must define this function.  Truncate locked node NODE to be SIZE
   bytes long.  (If NODE is already less than or equal to SIZE bytes
   long, do nothing.)  If this is a symlink (and diskfs_shortcut_symlink
   is set) then this should clear the symlink, even if
   diskfs_create_symlink_hook stores the link target elsewhere.  */
error_t
diskfs_truncate (struct node *node, loff_t length)
{
  error_t err;
  loff_t offset;

  diskfs_check_readonly ();
  assert (!diskfs_readonly);

  if (length >= node->dn_stat.st_size)
    return 0;

  /* If the file is not being truncated to a cluster boundary, the
     contents of the partial cluster following the end of the file
     must be zeroed in case it ever becomes accessible again because
     of subsequent file growth.  */
  offset = length & (bytes_per_cluster - 1);
  if (offset > 0)
    {
      diskfs_node_rdwr (node, (void *)zerocluster, length, bytes_per_cluster - offset,
                        1, 0, 0);
      diskfs_file_update (node, 1);
    }

  rwlock_writer_lock (&node->dn->alloc_lock);

  /* Update the size on disk; if we crash, we'll loose.  */
  node->dn_stat.st_size = length;
  node->dn_set_mtime = 1;
  node->dn_set_ctime = 1;
  diskfs_node_update (node, 1);

  err = diskfs_catch_exception ();
  if (!err)
    {
      fat_truncate_node(node, round_cluster(length) >> log2_bytes_per_cluster);
      node->allocsize = round_cluster(length);
    }
  diskfs_end_catch_exception ();

  node->dn_set_mtime = 1;
  node->dn_set_ctime = 1;
  node->dn_stat_dirty = 1;

  rwlock_writer_unlock (&node->dn->alloc_lock);
  
  return err;
}

error_t
diskfs_S_file_get_storage_info (struct protid *cred,
				mach_port_t **ports,
				mach_msg_type_name_t *ports_type,
				mach_msg_type_number_t *num_ports,
				int **ints, mach_msg_type_number_t *num_ints,
				loff_t **offsets,
				mach_msg_type_number_t *num_offsets,
				char **data, mach_msg_type_number_t *data_len)
{
  /* XXX */
  return EOPNOTSUPP;
}

/* Free node NP; the on disk copy has already been synced with
   diskfs_node_update (where NP->dn_stat.st_mode was 0).  It's
   mode used to be OLD_MODE.  */
void
diskfs_free_node (struct node *np, mode_t old_mode)
{
  assert (!diskfs_readonly);

  vi_free(np->dn->inode);
}

/* The user must define this function.  Allocate a new node to be of
   mode MODE in locked directory DP (don't actually set the mode or
   modify the dir, that will be done by the caller); the user
   responsible for the request can be identified with CRED.  Set *NP
   to be the newly allocated node.  */
error_t
diskfs_alloc_node (struct node *dir, mode_t mode, struct node **node)
{
  error_t err;
  ino_t inum;
  inode_t inode;
  struct node *np;
  
  assert (!diskfs_readonly);

  err = vi_new((struct vi_key) {0,1} /* XXX not allocated yet */, &inum, &inode);
  if (err)
    return err;

  err = diskfs_cached_lookup (inum, &np);
  if (err)
    return err;

  *node = np;
  return 0;
}
