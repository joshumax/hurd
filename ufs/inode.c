/* Inode management routines
   Copyright (C) 1994 Free Software Foundation

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

#include "ufs.h"
#include "dinode.h"
#include "fs.h"
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
  mutex_init (&dinmaplock);
  mutex_init (&sinmaplock);
}

/* Fetch inode INUM, set *NPP to the node structure; 
   gain one user reference and lock the node.  */
error_t 
iget (ino_t inum, struct node **npp)
{
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

  mutex_init (&dn->rwlock_master);
  condition_init (&dn->rwlock_wakeup);
  rwlock_init (&dn->dinlock);
  rwlock_init (&dn->sinlock);
  rwlock_init (&dn->datalock);
  dn->dinloc = 0;
  dn->sinloc = 0;
  dn->dinloclen = 0;
  dn->sinloclen = 0;
  dn->sininfo = 0;
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
  if (lblkno (sblock, np->dn_stat.st_size) < NDADDR)
    np->allocsize = fragroundup (sblock, np->dn_stat.st_size);
  else
    np->allocsize = blkroundup (sblock, np->dn_stat.st_size);
  
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
  if (np->dn->sininfo || np->dn->fileinfo || np->dn->dinloc
      || np->dn->sinloc || np->dn->dinloclen || np->dn->sinloclen)
    {
      printf ("I=%d\n", np->dn->number);
      printf ("Hard %d\tSoft %d\n", np->references, np->light_references);
      fflush (stdout);
    }
  assert (!np->dn->sininfo);
  assert (!np->dn->fileinfo);
  assert (!np->dn->dinloc);
  assert (!np->dn->sinloc);
  assert (!np->dn->dinloclen);
  assert (!np->dn->sinloclen);
  free (np->dn);
  free (np);
}

/* The last hard referencs to a node has gone away; arrange to have
   all the weak references dropped that can be. */
void
diskfs_lost_hardrefs (struct node *np)
{
  drop_pager_softrefs (np);
}

/* A new hard reference to a node has been created; it's now OK to
   have unused weak references. */
void
diskfs_new_hardrefs (struct node *np)
{
  allow_pager_softrefs (np);
}

/* Read stat information out of the dinode. */
static error_t
read_disknode (struct node *np)
{
  struct stat *st = &np->dn_stat;
  struct dinode *di = &dinodes[np->dn->number];
  error_t err;
  volatile long long pid = getpid ();
  
  err = diskfs_catch_exception ();
  if (err)
    return err;

  st->st_fstype = FSTYPE_UFS;
  st->st_fsid = pid;
  st->st_ino = np->dn->number;
  st->st_gen = di->di_gen;
  st->st_rdev = di->di_rdev;
  st->st_mode = di->di_model | (di->di_modeh << 16);
  st->st_nlink = di->di_nlink;
  st->st_size = di->di_size;
#ifdef notyet
  st->st_atimespec = di->di_atime;
  st->st_mtimespec = di->di_mtime;
  st->st_ctimespec = di->di_ctime;
#else
  st->st_atime = di->di_atime.ts_sec;
  st->st_atime_usec = di->di_atime.ts_nsec / 1000;
  st->st_mtime = di->di_mtime.ts_sec;
  st->st_mtime_usec = di->di_mtime.ts_nsec / 1000;
  st->st_ctime = di->di_ctime.ts_sec;
  st->st_ctime_usec = di->di_ctime.ts_nsec / 1000;
#endif  
  st->st_blksize = sblock->fs_bsize;
  st->st_blocks = di->di_blocks;
  st->st_flags = di->di_flags;
  
  if (sblock->fs_inodefmt < FS_44INODEFMT)
    {
      st->st_uid = di->di_ouid;
      st->st_gid = di->di_ogid;
      st->st_author = 0;
    }
  else
    {
      st->st_uid = di->di_uid;
      st->st_gid = di->di_gid;
      st->st_author = di->di_author;
      if (st->st_author == -1)
	st->st_author = st->st_uid;
    }

  diskfs_end_catch_exception ();
  if (!S_ISBLK (st->st_mode) && !S_ISCHR (st->st_mode))
    st->st_rdev = 0;
  return 0;
}

static void
write_node (struct node *np)
{
  struct stat *st = &np->dn_stat;
  struct dinode *di = &dinodes[np->dn->number];
  error_t err;
  
  assert (!np->dn_set_ctime && !np->dn_set_atime && !np->dn_set_mtime);
  if (np->dn_stat_dirty)
    {
      assert (!diskfs_readonly);

      err = diskfs_catch_exception ();
      if (err)
	return;
  
      di->di_gen = st->st_gen;
      
      if (S_ISBLK (st->st_mode) || S_ISCHR (st->st_mode))
	di->di_rdev = st->st_rdev;

      /* We happen to know that the stat mode bits are the same
	 as the ufs mode bits. */

      if (compat_mode == COMPAT_GNU)
	{
	  di->di_model = st->st_mode & 0xffff;
	  di->di_modeh = (st->st_mode >> 16) & 0xffff;
	}
      else 
	{
	  di->di_model = st->st_mode & 0xffff;
	  di->di_modeh = 0;
	}

      if (compat_mode != COMPAT_BSD42)
	{
	  di->di_uid = st->st_uid;
	  di->di_gid = st->st_gid;
	}
      
      if (sblock->fs_inodefmt < FS_44INODEFMT)
	{
	  di->di_ouid = st->st_uid & 0xffff;
	  di->di_ogid = st->st_gid & 0xffff;
	}
      else if (compat_mode == COMPAT_GNU)
	di->di_author = st->st_author;

      di->di_nlink = st->st_nlink;
      di->di_size = st->st_size;
#ifdef notyet
      di->di_atime = st->st_atimespec;
      di->di_mtime = st->st_mtimespec;
      di->di_ctime = st->st_ctimespec;
#else
      di->di_atime.ts_sec = st->st_atime;
      di->di_atime.ts_nsec = st->st_atime_usec * 1000;
      di->di_mtime.ts_sec = st->st_mtime;
      di->di_mtime.ts_nsec = st->st_mtime_usec * 1000;
      di->di_ctime.ts_sec = st->st_ctime;
      di->di_ctime.ts_nsec = st->st_ctime_usec * 1000;
#endif      
      di->di_blocks = st->st_blocks;
      di->di_flags = st->st_flags;
  
      diskfs_end_catch_exception ();
      np->dn_stat_dirty = 0;
    }
}  

/* See if we should create a symlink by writing it directly into
   the block pointer array.  Returning EINVAL tells diskfs to do it
   the usual way.  */
static error_t
create_symlink_hook (struct node *np, char *target)
{
  int len = strlen (target);
  error_t err;
  
  if (!direct_symlink_extension)
    return EINVAL;
  
  assert (compat_mode != COMPAT_BSD42);

  if (len >= sblock->fs_maxsymlinklen)
    return EINVAL;
  
  err = diskfs_catch_exception ();
  if (err)
    return err;
  
  bcopy (target, dinodes[np->dn->number].di_shortlink, len);
  np->dn_stat.st_size = len;
  np->dn_set_ctime = 1;
  np->dn_set_mtime = 1;

  diskfs_end_catch_exception ();
  return 0;
}
error_t (*diskfs_create_symlink_hook)(struct node *, char *) 
     = create_symlink_hook;

/* Check if this symlink is stored directly in the block pointer array.
   Returning EINVAL tells diskfs to do it the usual way. */
static error_t 
read_symlink_hook (struct node *np,
		   char *buf)
{
  error_t err;
  
  if (!direct_symlink_extension 
      || np->dn_stat.st_size >= sblock->fs_maxsymlinklen)
    return EINVAL;

  err = diskfs_catch_exception ();
  if (err)
    return err;
  
  bcopy (dinodes[np->dn->number].di_shortlink, buf, np->dn_stat.st_size);
  np->dn_set_atime = 1;

  diskfs_end_catch_exception ();
  return 0;
}
error_t (*diskfs_read_symlink_hook)(struct node *, char *)
     = read_symlink_hook;
     

/* Write all active disknodes into the dinode pager. */
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
  sync_dinode (np, wait);
}


/* Implement the diskfs_set_statfs callback from the diskfs library;
   see <hurd/diskfs.h> for the interface description.  */
error_t
diskfs_set_statfs (struct fsys_statfsbuf *st)
{
  st->fsys_stb_type = FSTYPE_UFS;
  st->fsys_stb_bsize = sblock->fs_bsize;
  st->fsys_stb_fsize = sblock->fs_fsize;
  st->fsys_stb_blocks = sblock->fs_dsize;
  st->fsys_stb_bfree = (sblock->fs_cstotal.cs_nbfree * sblock->fs_frag
		       + sblock->fs_cstotal.cs_nffree);
  st->fsys_stb_bavail = ((sblock->fs_dsize * (100 - sblock->fs_minfree) / 100)
			- (sblock->fs_dsize - st->fsys_stb_bfree));
  st->fsys_stb_files = sblock->fs_ncg * sblock->fs_ipg - 2; /* not 0 or 1 */
  st->fsys_stb_ffree = sblock->fs_cstotal.cs_nifree;
  st->fsys_stb_fsid = getpid ();
  return 0;
}

/* Implement the diskfs_set_translator callback from the diskfs
   library; see <hurd/diskfs.h> for the interface description. */
error_t
diskfs_set_translator (struct node *np, char *name, u_int namelen,
		       struct protid *cred)
{
  daddr_t blkno;
  error_t err;
  char buf[sblock->fs_bsize];
  
  if (compat_mode != COMPAT_GNU)
    return EOPNOTSUPP;

  if (namelen + sizeof (u_int) > sblock->fs_bsize)
    return ENAMETOOLONG;

  err = diskfs_catch_exception ();
  if (err)
    return err;
  
  blkno = dinodes[np->dn->number].di_trans;
  
  if (namelen && !blkno)
    {
      /* Allocate block for translator */
      err = ffs_alloc (np, 0, 0, sblock->fs_bsize, &blkno, cred);
      if (err)
	{
	  diskfs_end_catch_exception ();
	  return err;
	}
      dinodes[np->dn->number].di_trans = blkno;
      np->dn_set_ctime = 1;
    }
  else if (!namelen && blkno)
    {
      /* Clear block for translator going away. */
      ffs_blkfree (np, blkno, sblock->fs_bsize);
      dinodes[np->dn->number].di_trans = 0;
      np->dn_set_ctime = 1;
    }
  
  diskfs_end_catch_exception ();
  
  if (namelen)
    {
      bcopy (&namelen, buf, sizeof (u_int));
      bcopy (name, buf + sizeof (u_int), namelen);
      err = dev_write_sync (fsbtodb (sblock, blkno), 
			    (vm_address_t)buf, sblock->fs_bsize);
      np->dn_set_ctime = 1;
    }
  return err;
}

/* Implement the diskfs_get_translator callback from the diskfs library.
   See <hurd/diskfs.h> for the interface description. */
error_t
diskfs_get_translator (struct node *np, char **namep, u_int *namelen)
{
  error_t err;
  daddr_t blkno;
  char *buf;
  u_int datalen;

  err = diskfs_catch_exception ();
  if (err)
    return err;
  blkno = dinodes[np->dn->number].di_trans;
  diskfs_end_catch_exception ();
  
  assert (blkno);
  
  err = dev_read_sync (fsbtodb (sblock, blkno), (vm_address_t *)&buf,
		       sblock->fs_bsize);
  if (err)
    return err;
  
  datalen = *(u_int *)buf;
  if (datalen > *namelen)
    vm_allocate (mach_task_self (), (vm_address_t *) namep, datalen, 1);
  bcopy (buf + sizeof (u_int), *namep, datalen);
  *namelen = datalen;
  return 0;
}

/* Implement the diskfs_node_translated callback from the diskfs library.
   See <hurd/diskfs.h> for the interface description. */
int
diskfs_node_translated (struct node *np)
{
  int ret;
  
  if (diskfs_catch_exception ())
    return 0;
  
  ret = !! dinodes[np->dn->number].di_trans;
  diskfs_end_catch_exception ();
  return ret;
}

/* Called when all hard ports have gone away. */
void
diskfs_shutdown_soft_ports ()
{
  /* Should initiate termination of internally held pager ports
     (the only things that should be soft) XXX */
}

