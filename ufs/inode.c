/* Inode management routines
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <netinet/in.h>

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
diskfs_cached_lookup (int inum, struct node **npp)
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

  np->cache_id = dn->number = inum;
  dn->dirents = 0;

  rwlock_init (&dn->allocptrlock);
  dn->dirty = 0;
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

/* Read stat information out of the dinode. */
static error_t
read_disknode (struct node *np)
{
  static int fsid, fsidset;
  struct stat *st = &np->dn_stat;
  struct dinode *di = dino (np->dn->number);
  error_t err;
  
  err = diskfs_catch_exception ();
  if (err)
    return err;

  np->istranslated = !! di->di_trans;

  if (!fsidset)
    {
      fsid = getpid ();
      fsidset = 1;
    }

  st->st_fstype = FSTYPE_UFS;
  st->st_fsid = fsid;
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

  if (lblkno (sblock, np->dn_stat.st_size) < NDADDR)
    np->allocsize = fragroundup (sblock, np->dn_stat.st_size);
  else
    np->allocsize = blkroundup (sblock, np->dn_stat.st_size);

  return 0;
}

error_t diskfs_node_reload (struct node *node)
{
  if (node->dn->dirents)
    {
      free (node->dn->dirents);
      node->dn->dirents = 0;
    }
  flush_node_pager (node);
  read_disknode (node);
  return 0;
}

static void
write_node (struct node *np)
{
  struct stat *st = &np->dn_stat;
  struct dinode *di = dino (np->dn->number);
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
      record_poke (di, sizeof (struct dinode));
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
  struct dinode *di;
  
  if (!direct_symlink_extension)
    return EINVAL;
  
  assert (compat_mode != COMPAT_BSD42);

  if (len >= sblock->fs_maxsymlinklen)
    return EINVAL;
  
  err = diskfs_catch_exception ();
  if (err)
    return err;
  
  di = dino (np->dn->number);
  bcopy (target, di->di_shortlink, len);
  np->dn_stat.st_size = len;
  np->dn_set_ctime = 1;
  np->dn_set_mtime = 1;
  record_poke (di, sizeof (struct dinode));

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
  
  bcopy ((dino (np->dn->number))->di_shortlink, buf, np->dn_stat.st_size);

  if (! diskfs_readonly)
    np->dn_set_atime = 1;

  diskfs_end_catch_exception ();
  return 0;
}
error_t (*diskfs_read_symlink_hook)(struct node *, char *)
     = read_symlink_hook;

error_t
diskfs_node_iterate (error_t (*fun)(struct node *))
{
  struct node *np;
  struct item {struct item *next; struct node *np;} *list = 0;
  struct item *i;
  error_t err;
  int n;
      
  /* Acquire a reference on all the nodes in the hash table
     and enter them into a list on the stack. */
  spin_lock (&diskfs_node_refcnt_lock);
  for (n = 0; n < INOHSZ; n++)
    for (np = nodehash[n]; np; np = np->dn->hnext)
      {
	np->references++;
	i = alloca (sizeof (struct item));
	i->next = list;
	i->np = np;
	list = i;
      }
  spin_unlock (&diskfs_node_refcnt_lock);

  err = 0;
  for (i = list; i; i = i->next)
    {
      if (!err)
	{
	  mutex_lock (&i->np->lock);
	  err = (*fun)(i->np);
	  mutex_unlock (&i->np->lock);
	}
      diskfs_nrele (i->np);
    }
  return err;
}

/* Write all active disknodes into the dinode pager. */
void
write_all_disknodes ()
{
  error_t
    helper (struct node *np)
      {
	diskfs_set_node_times (np);
	write_node (np);
	return 0;
      }
  
  diskfs_node_iterate (helper);
}
	
void
diskfs_write_disknode (struct node *np, int wait)
{
  write_node (np);
  sync_dinode (np->dn->number, wait);
}

/* Implement the diskfs_set_statfs callback from the diskfs library;
   see <hurd/diskfs.h> for the interface description.  */
error_t
diskfs_set_statfs (struct fsys_statfsbuf *st)
{
  st->fsys_stb_type = FSTYPE_UFS;
  st->fsys_stb_iosize = sblock->fs_bsize;
  st->fsys_stb_bsize = sblock->fs_fsize;
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
  struct dinode *di;

  if (compat_mode != COMPAT_GNU)
    return EOPNOTSUPP;

  if (namelen + sizeof (u_int) > sblock->fs_bsize)
    return ENAMETOOLONG;

  err = diskfs_catch_exception ();
  if (err)
    return err;
  
  di = dino (np->dn->number);
  blkno = di->di_trans;
  
  if (namelen && !blkno)
    {
      /* Allocate block for translator */
      err = ffs_alloc (np, 0, 0, sblock->fs_bsize, &blkno, cred);
      if (err)
	{
	  diskfs_end_catch_exception ();
	  return err;
	}
      di->di_trans = blkno;
      record_poke (di, sizeof (struct dinode));
      np->dn_set_ctime = 1;
    }
  else if (!namelen && blkno)
    {
      /* Clear block for translator going away. */
      ffs_blkfree (np, blkno, sblock->fs_bsize);
      di->di_trans = 0;
      record_poke (di, sizeof (struct dinode));
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
}

/* Implement the diskfs_get_translator callback from the diskfs library.
   See <hurd/diskfs.h> for the interface description. */
error_t
diskfs_get_translator (struct node *np, char **namep, u_int *namelen)
{
  error_t err;
  daddr_t blkno;
  u_int datalen;
  void *transloc;

  err = diskfs_catch_exception ();
  if (err)
    return err;

  blkno = (dino (np->dn->number))->di_trans;
  assert (blkno);
  transloc = disk_image + fsaddr (sblock, blkno);
  
  datalen = *(u_int *)transloc;
  *namep = malloc (datalen);
  bcopy (transloc + sizeof (u_int), *namep, datalen);

  diskfs_end_catch_exception ();

  *namelen = datalen;
  return 0;
}

/* Called when all hard ports have gone away. */
void
diskfs_shutdown_soft_ports ()
{
  /* Should initiate termination of internally held pager ports
     (the only things that should be soft) XXX */
}

/* Return a description of the storage of the file. */
/* In STORAGE_DATA are the following, in network byte order:

   Inode number (4 bytes)
   disk address of transator spec (4 bytes)
   disk address of inode structure (4 bytes)
   offset into inode block holding inode (4 bytes) */
error_t
diskfs_S_file_get_storage_info (struct protid *cred,
				int *class,
				off_t **addresses,
				u_int *naddresses,
				size_t *block_size,
				char *storage_name,
				mach_port_t *storage_port,
				mach_msg_type_name_t *storage_port_type,
				char **storage_data,
				u_int *storage_data_len,
				int *flags)
{
  error_t err;
  struct node *np;
  int i;
  struct dinode *di;
  void *cp;
  
  np = cred->po->np;
  mutex_lock (&np->lock);
  
  /* See if this file fits in the direct block pointers.  If not, punt
     for now.  (Reading indir blocks is a pain, and I'm postponing
     pain.)  XXX */

  if (np->allocsize > NDADDR * sblock->fs_bsize)
    {
      mutex_unlock (&np->lock);
      return EINVAL;
    }
  
  if (*naddresses < NDADDR * 2)
    vm_allocate (mach_task_self (), (vm_address_t *) addresses, 
		 sizeof (int) * NDADDR * 2, 1);
  else
    bzero (addresses, *naddresses * 2 * sizeof (int));
  *naddresses = NDADDR * 2;

  if (*storage_data_len < 4 * sizeof (int))
    vm_allocate (mach_task_self (), (vm_address_t *) storage_data, 
		 sizeof (int) * 4, 1);
  *storage_data_len = 4 * sizeof (int);

  di = dino (np->dn->number);
  
  err = diskfs_catch_exception ();
  if (err)
    {
      mutex_unlock (&np->lock);
      return err;
    }
  
  /* Copy the block pointers */

  if (!direct_symlink_extension
      || np->dn_stat.st_size >= sblock->fs_maxsymlinklen
      || !S_ISLNK (np->dn_stat.st_mode))
    {
      for (i = 0; i < NDADDR; i++)
	{
	  (*addresses)[2 * i] = fsbtodb (sblock, di->di_db[i]);
	  if ((i + 1) * sblock->fs_bsize > np->allocsize)
	    (*addresses)[2 * i + 1] = np->allocsize - i * sblock->fs_bsize;
	  else
	    (*addresses)[2 * i + 1] = sblock->fs_bsize;
	}
    }

  /* Fill in the aux data */
  cp = *storage_data;

  *(int *)cp = htonl (np->dn->number);
  cp += sizeof (int);
  
  *(int *)cp = htonl (di->di_trans);
  cp += sizeof (int);
  
  *(int *)cp = htonl (fsbtodb (sblock, ino_to_fsba (sblock, np->dn->number)));
  cp += sizeof (int);
  
  *(int *)cp = htonl (ino_to_fsbo (sblock, np->dn->number)
		      * sizeof (struct dinode));
  
      
  diskfs_end_catch_exception ();
  
  *class = STORAGE_DEVICE;
  *flags = 0;
  *block_size = DEV_BSIZE;

  if (diskfs_device_name)
    strcpy (storage_name, diskfs_device_name);
  
  if (diskfs_isuid (0, cred))
    *storage_port = diskfs_device;
  else
    *storage_port = MACH_PORT_NULL;
  *storage_port_type = MACH_MSG_TYPE_COPY_SEND;
  
  mutex_unlock (&np->lock);
  
  return 0;
}

  
