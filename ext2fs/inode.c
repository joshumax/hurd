/* Inode management routines

   Copyright (C) 1994, 1995 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "ext2fs.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#define	INOHSZ	512
#if	((INOHSZ&(INOHSZ-1)) == 0)
#define	INOHASH(ino)	((ino)&(INOHSZ-1))
#else
#define	INOHASH(ino)	(((unsigned)(ino))%INOHSZ)
#endif

static struct node *nodehash[INOHSZ];
static error_t read_disknode (struct node *np);

spin_lock_t gennumberlock = SPIN_LOCK_INITIALIZER;

/* Initialize the inode hash table. */
void
inode_init ()
{
  int n;
  for (n = 0; n < INOHSZ; n++)
    nodehash[n] = 0;
}

/* Fetch inode INUM, set *NPP to the node structure; 
   gain one user reference and lock the node.  */
error_t 
iget (ino_t inum, struct node **npp)
{
  int offset;
  struct disknode *dn;
  struct node *np;
  error_t err;

  spin_lock (&diskfs_node_refcnt_lock);
  for (np = nodehash[INOHASH(inum)]; np; np = np->dn->hnext)
    {
      if (np->dn->number != inum)
	continue;

      np->references++;
      spin_unlock (&diskfs_node_refcnt_lock);
      mutex_lock (&np->lock);
      *npp = np;
      return 0;
    }

  dn = malloc (sizeof (struct disknode));

  dn->number = inum;
  dn->dirents = 0;

  rwlock_init (&dn->alloc_lock);
  pokel_init (&dn->pokel, disk_pager->p, disk_image);
  dn->fileinfo = 0;

  np = diskfs_make_node (dn);
  mutex_lock (&np->lock);
  dn->hnext = nodehash[INOHASH(inum)];
  if (dn->hnext)
    dn->hnext->dn->hprevp = &dn->hnext;
  dn->hprevp = &nodehash[INOHASH(inum)];
  nodehash[INOHASH(inum)] = np;
  spin_unlock (&diskfs_node_refcnt_lock);

  err = read_disknode (np);

  np->allocsize = np->dn_stat.st_size;
  offset = np->allocsize % block_size;
  if (offset > 0)
    np->allocsize += block_size - offset;
  
  if (!diskfs_readonly && !np->dn_stat.st_gen)
    {
      spin_lock (&gennumberlock);
      if (++nextgennumber < diskfs_mtime->seconds)
	nextgennumber = diskfs_mtime->seconds;
      np->dn_stat.st_gen = nextgennumber;
      spin_unlock (&gennumberlock);
      np->dn_set_ctime = 1;
    }
  
  if (err)
    return err;
  else
    {
      *npp = np;
      return 0;
    }
}

/* Lookup node INUM (which must have a reference already) and return it
   without allocating any new references. */
struct node *
ifind (ino_t inum)
{
  struct node *np;
  
  spin_lock (&diskfs_node_refcnt_lock);
  for (np = nodehash[INOHASH(inum)]; np; np = np->dn->hnext)
    {
      if (np->dn->number != inum)
	continue;
      
      assert (np->references);
      spin_unlock (&diskfs_node_refcnt_lock);
      return np;
    }
  assert (0);
}

/* The last reference to a node has gone away; drop
   it from the hash table and clean all state in the dn structure. */
void      
diskfs_node_norefs (struct node *np)
{
  *np->dn->hprevp = np->dn->hnext;
  if (np->dn->hnext)
    np->dn->hnext->dn->hprevp = np->dn->hprevp;
  if (np->dn->dirents)
    free (np->dn->dirents);
  assert (!np->dn->fileinfo);
  free (np->dn);
  free (np);
}

/* The last hard reference to a node has gone away; arrange to have
   all the weak references dropped that can be. */
void
diskfs_try_dropping_softrefs (struct node *np)
{
  drop_pager_softrefs (np);
}

/* The last hard reference to a node has gone away. */
void
diskfs_lost_hardrefs (struct node *np)
{
#ifdef notanymore
  struct port_info *pi;
  struct pager *p;
  
  /* Check and see if there is a pager which has only
     one reference (ours).  If so, then drop that reference,
     breaking the cycle.  The complexity in this routine
     is all due to this cycle.  */

  if (np->dn->fileinfo)
    {
      spin_lock (&_libports_portrefcntlock);
      pi = (struct port_info *) np->dn->fileinfo->p;
      if (pi->refcnt == 1)
	{
	  
	  /* The only way to get a new reference to the pager
	     in this state is to call diskfs_get_filemap; this
	     can't happen as long as we hold NP locked.  So
	     we can safely unlock _libports_portrefcntlock for
	     the following call. */
	  spin_unlock (&_libports_portrefcntlock);
	  
	  /* Right now the node is locked with no hard refs;
	     this is an anomolous situation.  Before messing with
	     the reference count on the file pager, we have to 
	     give ourselves a reference back so that we are really
	     allowed to hold the lock.  Then we can do the
	     unreference. */
	  p = np->dn->fileinfo->p;
	  np->dn->fileinfo = 0;
	  diskfs_nref (np);
	  pager_unreference (p);

	  assert (np->references == 1 && np->light_references == 0);

	  /* This will do the real deallocate.  Whew. */
	  diskfs_nput (np);
	}
      else
	spin_unlock (&_libports_portrefcntlock);
    }
#endif
}

/* A new hard reference to a node has been created; it's now OK to
   have unused weak references. */
void
diskfs_new_hardrefs (struct node *np)
{
  allow_pager_softrefs (np);
}

/* Read stat information out of the ext2_inode. */
static error_t
read_disknode (struct node *np)
{
  static int fsid, fsidset;
  struct stat *st = &np->dn_stat;
  struct ext2_inode *di = dino (np->dn->number);
  struct ext2_inode_info *info = &np->dn->info;
  error_t err;
  
  err = diskfs_catch_exception ();
  if (err)
    return err;

  np->istranslated = !! di->i_translator;

  if (!fsidset)
    {
      fsid = getpid ();
      fsidset = 1;
    }

  st->st_fstype = FSTYPE_EXT2FS;
  st->st_fsid = fsid;
  st->st_ino = np->dn->number;
  st->st_blksize = block_size;

  st->st_mode = di->i_mode | (di->i_mode_high << 16);
  st->st_nlink = di->i_links_count;
  st->st_size = di->i_size;
  st->st_gen = di->i_version;

  st->st_atime = di->i_atime;
  st->st_mtime = di->i_mtime;
  st->st_ctime = di->i_ctime;

#ifdef XXX
  st->st_atime_usec = di->i_atime.ts_nsec / 1000;
  st->st_mtime_usec = di->i_mtime.ts_nsec / 1000;
  st->st_ctime_usec = di->i_ctime.ts_nsec / 1000;
#endif

  st->st_blocks = di->i_blocks;
  st->st_flags = di->i_flags;
  
  st->st_uid = di->i_uid | (di->i_uid_high << 16);
  st->st_gid = di->i_gid | (di->i_gid_high << 16);
  st->st_author = di->i_author;
  if (st->st_author == -1)
    st->st_author = st->st_uid;

  /* Setup the ext2fs auxiliary inode info.  */
  info->i_dtime = di->i_dtime;
  info->i_flags = di->i_flags;
  info->i_faddr = di->i_faddr;
  info->i_frag_no = di->i_frag;
  info->i_frag_size = di->i_fsize;
  info->i_osync = 0;
  info->i_file_acl = di->i_file_acl;
  info->i_dir_acl = di->i_dir_acl;
  info->i_version = di->i_version;
  info->i_block_group = inode_group_num(np->dn->number);
  info->i_next_alloc_block = 0;
  info->i_next_alloc_goal = 0;
  if (info->i_prealloc_count)
    ext2_error ("ext2_read_inode", "New inode has non-zero prealloc count!");
  if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
    st->st_rdev = di->i_block[0];
  else
    {
      int block;
      for (block = 0; block < EXT2_N_BLOCKS; block++)
	info->i_data[block] = di->i_block[block];
      st->st_rdev = 0;
    }

  diskfs_end_catch_exception ();

  return 0;
}

static void
write_node (struct node *np)
{
  struct stat *st = &np->dn_stat;
  struct ext2_inode *di = dino (np->dn->number);
  error_t err;
  
  assert (!np->dn_set_ctime && !np->dn_set_atime && !np->dn_set_mtime);
  if (np->dn_stat_dirty)
    {
      assert (!diskfs_readonly);

      err = diskfs_catch_exception ();
      if (err)
	return;
  
      di->i_version = st->st_gen;

      /* We happen to know that the stat mode bits are the same
	 as the ext2fs mode bits. */
      /* XXX? */

      di->i_mode = st->st_mode & 0xffff;
      di->i_mode_high = (st->st_mode >> 16) & 0xffff;
      
      di->i_uid = st->st_uid & 0xFFFF;
      di->i_gid = st->st_gid & 0xFFFF;
      di->i_uid_high = st->st_uid >> 16;
      di->i_gid_high = st->st_gid >> 16;

      di->i_author = st->st_author;

      di->i_links_count = st->st_nlink;
      di->i_size = st->st_size;

      di->i_atime = st->st_atime;
      di->i_mtime = st->st_mtime;
      di->i_ctime = st->st_ctime;
#ifdef XXX
      di->i_atime.ts_nsec = st->st_atime_usec * 1000;
      di->i_mtime.ts_nsec = st->st_mtime_usec * 1000;
      di->i_ctime.ts_nsec = st->st_ctime_usec * 1000;
#endif

      di->i_blocks = st->st_blocks;
      di->i_flags = st->st_flags;

      if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
	di->i_block[0] = st->st_rdev;
      else
	{
	  int block;
	  for (block = 0; block < EXT2_N_BLOCKS; block++)
	    di->i_block[block] = np->dn->info.i_data[block];
	}
  
      diskfs_end_catch_exception ();
      np->dn_stat_dirty = 0;
      pokel_add (&np->dn->pokel, di, sizeof (struct ext2_inode));
    }
}  

/* Write all active disknodes into the ext2_inode pager. */
void
write_all_disknodes ()
{
  int n;
  struct node *np;
  
  spin_lock (&diskfs_node_refcnt_lock);
  for (n = 0; n < INOHSZ; n++)
    for (np = nodehash[n]; np; np = np->dn->hnext)
      {
	diskfs_set_node_times (np);
	write_node (np);
      }
  spin_unlock (&diskfs_node_refcnt_lock);
}
	
void
diskfs_write_disknode (struct node *np, int wait)
{
  write_node (np);
  pokel_sync (&np->dn->pokel, wait);
}

/* Implement the diskfs_set_statfs callback from the diskfs library;
   see <hurd/diskfs.h> for the interface description.  */
error_t
diskfs_set_statfs (struct fsys_statfsbuf *st)
{
  st->fsys_stb_type = FSTYPE_EXT2FS;
  st->fsys_stb_fsize = EXT2_BLOCK_SIZE(sblock);
  st->fsys_stb_fsize = EXT2_FRAG_SIZE(sblock);
  st->fsys_stb_blocks = sblock->s_blocks_count;
  st->fsys_stb_bfree = sblock->s_free_blocks_count;
  st->fsys_stb_bavail = st->fsys_stb_bfree - sblock->s_r_blocks_count;
  st->fsys_stb_files = sblock->s_inodes_count;
  st->fsys_stb_ffree = sblock->s_free_inodes_count;
  st->fsys_stb_fsid = getpid ();
  return 0;
}

/* Implement the diskfs_set_translator callback from the diskfs
   library; see <hurd/diskfs.h> for the interface description. */
error_t
diskfs_set_translator (struct node *np, char *name, u_int namelen,
		       struct protid *cred)
{
#ifdef XXX
  daddr_t blkno;
  error_t err;
  char buf[sblock->fs_bsize];
  struct ext2_inode *di;

  if (compat_mode != COMPAT_GNU)
    return EOPNOTSUPP;

  if (namelen + sizeof (u_int) > sblock->fs_bsize)
    return ENAMETOOLONG;

  err = diskfs_catch_exception ();
  if (err)
    return err;
  
  di = dino (np->dn->number);
  blkno = di->i_trans;
  
  if (namelen && !blkno)
    {
      /* Allocate block for translator */
      err = ffs_alloc (np, 0, 0, sblock->fs_bsize, &blkno, cred);
      if (err)
	{
	  diskfs_end_catch_exception ();
	  return err;
	}
      di->i_trans = blkno;
      pokel_add (&np->dn.pokel, di, sizeof (struct ext2_inode));
      np->dn_set_ctime = 1;
    }
  else if (!namelen && blkno)
    {
      /* Clear block for translator going away. */
      ffs_blkfree (np, blkno, sblock->fs_bsize);
      di->i_trans = 0;
      pokel_add (&np->dn.pokel, di, sizeof (struct ext2_inode));
      np->dn_stat.st_blocks -= btodb (sblock->fs_bsize);
      np->istranslated = 0;
      np->dn_set_ctime = 1;
    }
  
  if (namelen)
    {
      bcopy (&namelen, buf, sizeof (u_int));
      bcopy (name, buf + sizeof (u_int), namelen);

      bcopy (buf, disk_image + fsaddr (sblock, blkno), sblock->fs_bsize);
      sync_disk_blocks (blkno, sblock->fs_bsize, 1);

      np->istranslated = 1;
      np->dn_set_ctime = 1;
    }
  
  diskfs_end_catch_exception ();
  return err;
#else
  return EOPNOTSUPP;
#endif
}

/* Implement the diskfs_get_translator callback from the diskfs library.
   See <hurd/diskfs.h> for the interface description. */
error_t
diskfs_get_translator (struct node *np, char **namep, u_int *namelen)
{
#ifdef XXX
  error_t err;
  daddr_t blkno;
  u_int datalen;
  void *transloc;

  err = diskfs_catch_exception ();
  if (err)
    return err;

  blkno = (dino (np->dn->number))->i_trans;
  assert (blkno);
  transloc = disk_image + fsaddr (sblock, blkno);
  
  datalen = *(u_int *)transloc;
  if (datalen > *namelen)
    vm_allocate (mach_task_self (), (vm_address_t *) namep, datalen, 1);
  bcopy (transloc + sizeof (u_int), *namep, datalen);

  diskfs_end_catch_exception ();

  *namelen = datalen;
  return 0;
#else
  return EOPNOTSUPP;
#endif
}

/* Called when all hard ports have gone away. */
void
diskfs_shutdown_soft_ports ()
{
  /* Should initiate termination of internally held pager ports
     (the only things that should be soft) XXX */
}

