/* Inode management routines

   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2007
     Free Software Foundation, Inc.

   Converted for ext2fs by Miles Bader <miles@gnu.org>

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
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>

/* these flags aren't actually defined by a header file yet, so temporarily
   disable them if necessary.  */
#ifndef UF_APPEND
#define UF_APPEND 0
#endif
#ifndef UF_NODUMP
#define UF_NODUMP 0
#endif
#ifndef UF_IMMUTABLE
#define UF_IMMUTABLE 0
#endif

pthread_spinlock_t generation_lock = PTHREAD_SPINLOCK_INITIALIZER;

/* The user must define this function if she wants to use the node
   cache.  Create and initialize a node.  */
error_t
diskfs_user_make_node (struct node **npp, struct lookup_context *ctx)
{
  struct node *np;
  struct disknode *dn;

  /* Create the new node.  */
  np = diskfs_make_node_alloc (sizeof *dn);
  if (np == NULL)
    return ENOMEM;

  /* Format specific data for the new node.  */
  dn = diskfs_node_disknode (np);
  dn->dirents = 0;
  dn->dir_idx = 0;
  dn->pager = 0;
  pthread_rwlock_init (&dn->alloc_lock, NULL);
  pokel_init (&dn->indir_pokel, diskfs_disk_pager, disk_cache);

  *npp = np;
  return 0;
}

/* The last reference to a node has gone away; drop
   it from the hash table and clean all state in the dn structure. */
void
diskfs_node_norefs (struct node *np)
{
  if (diskfs_node_disknode (np)->dirents)
    free (diskfs_node_disknode (np)->dirents);
  assert_backtrace (!diskfs_node_disknode (np)->pager);

  /* Move any pending writes of indirect blocks.  */
  pokel_inherit (&global_pokel, &diskfs_node_disknode (np)->indir_pokel);
  pokel_finalize (&diskfs_node_disknode (np)->indir_pokel);

  free (np);
}

/* The user must define this function if she wants to use the node
   cache.  The last hard reference to a node has gone away; arrange to
   have all the weak references dropped that can be.  */
void
diskfs_user_try_dropping_softrefs (struct node *np)
{
  drop_pager_softrefs (np);
}

/* The last hard reference to a node has gone away. */
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

/* The user must define this function if she wants to use the node
   cache.  Read stat information out of the on-disk node.  */
error_t
diskfs_user_read_node (struct node *np, struct lookup_context *ctx)
{
  error_t err;
  struct stat *st = &np->dn_stat;
  struct disknode *dn = diskfs_node_disknode (np);
  struct ext2_inode *di;
  struct ext2_inode_info *info = &dn->info;

  ext2_debug ("(%llu)", np->cache_id);

  err = diskfs_catch_exception ();
  if (err)
    return err;

  di = dino_ref (np->cache_id);

  st->st_fstype = FSTYPE_EXT2FS;
  st->st_fsid = getpid ();	/* This call is very cheap.  */
  st->st_ino = np->cache_id;
  st->st_blksize = vm_page_size * 2;

  st->st_nlink = di->i_links_count;
  st->st_size = di->i_size;
  st->st_gen = di->i_generation;

  st->st_atim.tv_sec = di->i_atime;
#ifdef not_yet
  /* ``struct ext2_inode'' doesn't do better than sec. precision yet.  */
#else
  st->st_atim.tv_nsec = 0;
#endif
  st->st_mtim.tv_sec = di->i_mtime;
#ifdef not_yet
  /* ``struct ext2_inode'' doesn't do better than sec. precision yet.  */
#else
  st->st_mtim.tv_nsec = 0;
#endif
  st->st_ctim.tv_sec = di->i_ctime;
#ifdef not_yet
  /* ``struct ext2_inode'' doesn't do better than sec. precision yet.  */
#else
  st->st_ctim.tv_nsec = 0;
#endif

  st->st_blocks = di->i_blocks;

  st->st_flags = 0;
  if (di->i_flags & EXT2_APPEND_FL)
    st->st_flags |= UF_APPEND;
  if (di->i_flags & EXT2_NODUMP_FL)
    st->st_flags |= UF_NODUMP;
  if (di->i_flags & EXT2_IMMUTABLE_FL)
    st->st_flags |= UF_IMMUTABLE;

  if (sblock->s_creator_os == EXT2_OS_HURD)
    {
      st->st_mode = di->i_mode | (di->i_mode_high << 16);
      st->st_mode &= ~S_ITRANS;

      if (di->i_translator)
	st->st_mode |= S_IPTRANS;
      else
	{
	  size_t datalen = 0;
	  err = ext2_get_xattr (np, "gnu.translator", NULL, &datalen);
	  if (! err && datalen > 0)
	    st->st_mode |= S_IPTRANS;
	}

      st->st_uid = di->i_uid | (di->i_uid_high << 16);
      st->st_gid = di->i_gid | (di->i_gid_high << 16);

      st->st_author = di->i_author;
      if (st->st_author == -1)
	st->st_author = st->st_uid;
    }
  else
    {
      st->st_mode = di->i_mode & ~S_ITRANS;
      st->st_uid = di->i_uid;
      st->st_gid = di->i_gid;
      st->st_author = st->st_uid;
      np->author_tracks_uid = 1;
    }

  /* Setup the ext2fs auxiliary inode info.  */
  info->i_dtime = di->i_dtime;
  info->i_flags = di->i_flags;
  info->i_faddr = di->i_faddr;
  info->i_frag_no = di->i_frag;
  info->i_frag_size = di->i_fsize;
  info->i_osync = 0;
  info->i_file_acl = di->i_file_acl;
  if (S_ISDIR (st->st_mode))
    info->i_dir_acl = di->i_dir_acl;
  else
    {
      info->i_dir_acl = 0;
      if (sizeof (off_t) >= 8)
	/* 64bit file size */
	st->st_size += ((off_t) di->i_size_high) << 32;
      else
	{
	  if (di->i_size_high)	/* XXX */
	    {
	      dino_deref (di);
	      ext2_warning ("cannot handle large file inode %Ld", np->cache_id);
	      diskfs_end_catch_exception ();
	      return EFBIG;
	    }
	}
    }
  info->i_block_group = inode_group_num (np->cache_id);
  info->i_next_alloc_block = 0;
  info->i_next_alloc_goal = 0;
  info->i_prealloc_count = 0;

  /* Set to a conservative value.  */
  dn->last_page_partially_writable = 0;

  if (S_ISCHR (st->st_mode) || S_ISBLK (st->st_mode))
    st->st_rdev = di->i_block[0];
  else
    {
      memcpy (info->i_data, di->i_block,
	      EXT2_N_BLOCKS * sizeof info->i_data[0]);
      st->st_rdev = 0;
    }
  dn->info_i_translator = di->i_translator;

  dino_deref (di);
  diskfs_end_catch_exception ();

  if (S_ISREG (st->st_mode) || S_ISDIR (st->st_mode)
      || (S_ISLNK (st->st_mode) && st->st_blocks))
    {
      unsigned offset;

      np->allocsize = np->dn_stat.st_size;

      /* Round up to a block multiple.  */
      offset = np->allocsize & ((1 << log2_block_size) - 1);
      if (offset > 0)
	np->allocsize += block_size - offset;
    }
  else
    /* Allocsize should be zero for anything except directories, files, and
       long symlinks.  These are the only things allowed to have any blocks
       allocated as well, although st_size may be zero for any type (cases
       where st_blocks=0 and st_size>0 include fast symlinks, and, under
       linux, some devices).  */
    np->allocsize = 0;

  if (!diskfs_check_readonly () && !np->dn_stat.st_gen)
    {
      pthread_spin_lock (&generation_lock);
      if (++next_generation < diskfs_mtime->seconds)
	next_generation = diskfs_mtime->seconds;
      np->dn_stat.st_gen = next_generation;
      pthread_spin_unlock (&generation_lock);
      np->dn_set_ctime = 1;
    }

  return 0;
}

/* Return EINVAL if this is not a hurd filesystem and any bits are set in L
   except the low 16 bits, else 0.  */
static inline error_t
check_high_bits (struct node *np, long l)
{
  if (sblock->s_creator_os == EXT2_OS_HURD)
    return 0;

  /* Linux 2.3.42 has a mount-time option (not a bit stored on disk)
     NO_UID32 to ignore the high 16 bits of uid and gid, but by default
     allows them.  It also does this check for "interoperability with old
     kernels".  Note that our check refuses to change the values, while
     Linux 2.3.42 just silently clears the high bits in an inode it updates,
     even if it was updating it for an unrelated reason.  */
  if (diskfs_node_disknode (np)->info.i_dtime != 0)
    return 0;

  return ((l & ~0xFFFF) == 0) ? 0 : EINVAL;
}

/* Return 0 if NP's owner can be changed to UID; otherwise return an error
   code. */
error_t
diskfs_validate_owner_change (struct node *np, uid_t uid)
{
  return check_high_bits (np, uid);
}

/* Return 0 if NP's group can be changed to GID; otherwise return an error
   code. */
error_t
diskfs_validate_group_change (struct node *np, gid_t gid)
{
  return check_high_bits (np, gid);
}

/* Return 0 if NP's mode can be changed to MODE; otherwise return an error
   code.  It must always be possible to clear the mode; diskfs will not ask
   for permission before doing so.  */
error_t
diskfs_validate_mode_change (struct node *np, mode_t mode)
{
  return check_high_bits (np, mode);
}

/* Return 0 if NP's author can be changed to AUTHOR; otherwise return an
   error code. */
error_t
diskfs_validate_author_change (struct node *np, uid_t author)
{
  if (sblock->s_creator_os == EXT2_OS_HURD)
    return 0;
  else
    /* For non-hurd filesystems, the author & owner are the same.  */
    return (author == np->dn_stat.st_uid) ? 0 : EINVAL;
}

/* The user may define this function.  Return 0 if NP's flags can be
   changed to FLAGS; otherwise return an error code.  It must always
   be possible to clear the flags.   */
error_t
diskfs_validate_flags_change (struct node *np, int flags)
{
  if (flags & ~(UF_NODUMP | UF_IMMUTABLE | UF_APPEND))
    return EINVAL;
  else
    return 0;
}

/* Writes everything from NP's inode to the disk image, and returns a pointer
   to it, or NULL if nothing need be done.  */
static struct ext2_inode *
write_node (struct node *np)
{
  error_t err;
  struct stat *st = &np->dn_stat;
  struct ext2_inode *di;

  ext2_debug ("(%llu)", np->cache_id);

  if (diskfs_node_disknode (np)->info.i_prealloc_count)
    ext2_discard_prealloc (np);

  if (np->dn_stat_dirty)
    {
      struct ext2_inode_info *info = &diskfs_node_disknode (np)->info;

      assert_backtrace (!diskfs_readonly);

      ext2_debug ("writing inode %d to disk", np->cache_id);

      err = diskfs_catch_exception ();
      if (err)
	return NULL;

      di = dino_ref (np->cache_id);

      di->i_generation = st->st_gen;

      /* We happen to know that the stat mode bits are the same
	 as the ext2fs mode bits. */
      /* XXX? */

      /* Only the low 16 bits of these fields are standard across all ext2
	 implementations.  */
      di->i_mode = st->st_mode & 0xFFFF & ~S_ITRANS;
      di->i_uid = st->st_uid & 0xFFFF;
      di->i_gid = st->st_gid & 0xFFFF;

      if (sblock->s_creator_os == EXT2_OS_HURD)
	/* If this is a hurd-compatible filesystem, write the high bits too. */
	{
	  di->i_mode_high = (st->st_mode >> 16) & 0xffff & ~S_ITRANS;
	  di->i_uid_high = st->st_uid >> 16;
	  di->i_gid_high = st->st_gid >> 16;
	  di->i_author = st->st_author;
	}
      else
	/* No hurd extensions should be turned on.  */
	{
	  assert_backtrace ((st->st_uid & ~0xFFFF) == 0);
	  assert_backtrace ((st->st_gid & ~0xFFFF) == 0);
	  assert_backtrace ((st->st_mode & ~0xFFFF) == 0);
	  assert_backtrace (np->author_tracks_uid && st->st_author == st->st_uid);
	}

      di->i_links_count = st->st_nlink;

      di->i_atime = st->st_atim.tv_sec;
#ifdef not_yet
      /* ``struct ext2_inode'' doesn't do better than sec. precision yet.  */
      di->i_atime.tv_nsec = st->st_atim.tv_nsec;
#endif
      di->i_mtime = st->st_mtim.tv_sec;
#ifdef not_yet
      di->i_mtime.tv_nsec = st->st_mtim.tv_nsec;
#endif
      di->i_ctime = st->st_ctim.tv_sec;
#ifdef not_yet
      di->i_ctime.tv_nsec = st->st_ctim.tv_nsec;
#endif

      /* Convert generic flags in ST->st_flags to ext2-specific flags in DI
         (but don't mess with ext2 flags we don't know about).  The original
	 set was copied from DI into INFO by read_node, but might have been
	 modified for ext2fs-specific reasons; so we use INFO->i_flags
	 to start with, and then apply the flags in ST->st_flags.  */
      info->i_flags &= ~(EXT2_APPEND_FL | EXT2_NODUMP_FL | EXT2_IMMUTABLE_FL);
      if (st->st_flags & UF_APPEND)
	info->i_flags |= EXT2_APPEND_FL;
      if (st->st_flags & UF_NODUMP)
	info->i_flags |= EXT2_NODUMP_FL;
      if (st->st_flags & UF_IMMUTABLE)
	info->i_flags |= EXT2_IMMUTABLE_FL;
      di->i_flags = info->i_flags;

      if (st->st_mode == 0)
	/* Set dtime non-zero to indicate a deleted file.
	   We don't clear i_size, i_blocks, and i_translator in this case,
	   to give "undeletion" utilities a chance.  */
	di->i_dtime = di->i_mtime;
      else
	{
	  di->i_dtime = 0;
	  di->i_size = st->st_size;
	  if (sizeof (off_t) >= 8 && !S_ISDIR (st->st_mode))
	    /* 64bit file size */
	    di->i_size_high = st->st_size >> 32;
	  di->i_blocks = st->st_blocks;
	}

      if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode))
	di->i_block[0] = st->st_rdev;
      else
	memcpy (di->i_block, diskfs_node_disknode (np)->info.i_data,
		EXT2_N_BLOCKS * sizeof di->i_block[0]);

      diskfs_end_catch_exception ();
      np->dn_stat_dirty = 0;

      /* Leave invoking dino_deref (di) to the caller.  */
      return di;
    }
  else
    return NULL;
}

/* Reload all data specific to NODE from disk, without writing anything.
   Always called with DISKFS_READONLY true.  */
error_t
diskfs_node_reload (struct node *node)
{
  struct disknode *dn = diskfs_node_disknode (node);

  if (dn->dirents)
    {
      free (dn->dirents);
      dn->dirents = 0;
    }
  pokel_flush (&dn->indir_pokel);
  flush_node_pager (node);
  diskfs_user_read_node (node, NULL);

  return 0;
}

/* Write all active disknodes into the ext2_inode pager. */
void
write_all_disknodes ()
{
  error_t write_one_disknode (struct node *node)
    {
      struct ext2_inode *di;

      /* Sync the indirect blocks here; they'll all be done before any
	 inodes.  Waiting for them shouldn't be too bad.  */
      pokel_sync (&diskfs_node_disknode (node)->indir_pokel, 1);

      diskfs_set_node_times (node);

      /* Update the inode image.  */
      di = write_node (node);
      if (di)
	record_global_poke (di);

      return 0;
    }

  diskfs_node_iterate (write_one_disknode);
}

/* Sync the info in NP->dn_stat and any associated format-specific
   information to disk.  If WAIT is true, then return only after the
   physicial media has been completely updated.  */
void
diskfs_write_disknode (struct node *np, int wait)
{
  struct ext2_inode *di = write_node (np);
  if (di)
    {
      if (wait)
	sync_global_ptr (di, 1);
      else
	record_global_poke (di);
    }
}

/* Set *ST with appropriate values to reflect the current state of the
   filesystem.  */
error_t
diskfs_set_statfs (struct statfs *st)
{
  st->f_type = FSTYPE_EXT2FS;
  st->f_bsize = block_size;
  st->f_blocks = sblock->s_blocks_count;
  st->f_bfree = sblock->s_free_blocks_count;
  st->f_bavail = st->f_bfree - sblock->s_r_blocks_count;
  if (st->f_bfree < sblock->s_r_blocks_count)
    st->f_bavail = 0;
  st->f_files = sblock->s_inodes_count;
  st->f_ffree = sblock->s_free_inodes_count;
  st->f_fsid = getpid ();
  st->f_namelen = 0;
  st->f_favail = st->f_ffree;
  st->f_frsize = frag_size;
  return 0;
}

/* Implement the diskfs_set_translator callback from the diskfs
   library; see <hurd/diskfs.h> for the interface description. */
error_t
diskfs_set_translator (struct node *np, const char *name, unsigned namelen,
		       struct protid *cred)
{
  error_t err;

  assert_backtrace (!diskfs_readonly);

  if (sblock->s_creator_os != EXT2_OS_HURD)
    return EOPNOTSUPP;

  err = diskfs_catch_exception ();
  if (err)
    return err;

  /* If xattr is supported for this filesystem, use xattr to store translator
     record, otherwise, use legacy translator record */
  if (EXT2_HAS_COMPAT_FEATURE (sblock, EXT2_FEATURE_COMPAT_EXT_ATTR)
      && use_xattr_translator_records)
    {
      daddr_t blkno;
      struct ext2_inode *di;

      di = dino_ref (np->cache_id);
      blkno = di->i_translator;

      /* If a legacy translator record found, clear it */
      if (blkno)
	{
	  ext2_debug ("Old translator record found, clear it");

	  /* Clear block for translator going away. */
	  di->i_translator = 0;
	  diskfs_node_disknode (np)->info_i_translator = 0;
	  record_global_poke (di);
	  ext2_free_blocks (blkno, 1);

	  np->dn_stat.st_blocks -= 1 << log2_stat_blocks_per_fs_block;
	  np->dn_stat.st_mode &= ~S_IPTRANS;
	  np->dn_set_ctime = 1;
	}
      else
	dino_deref (di);

      /* Use xattr to store translator record, with key "gnu.translator" */
      if (namelen)
	{
	  err = ext2_set_xattr (np, "gnu.translator", name, namelen, 0);

	  if (!err)
	    {
	      np->dn_stat.st_mode |= S_IPTRANS;
	      np->dn_set_ctime = 1;
	    }
	}
      else
	{
	  /* Removing the translator.  */
	  err = ext2_set_xattr (np, "gnu.translator", NULL, 0, 0);

	  if (err == ENODATA)
	    /* Happens if the key did not exist in the first place.  */
	    err = 0;

	  if (!err)
	    {
	      /* Do not use hurd extensions on non-hurd created filesystem */
	      np->dn_stat.st_mode &= ~S_IPTRANS;
	      np->dn_set_ctime = 1;
	    }
	}
    }
  else
    {
      /* Use legacy translator record when xattr is not supported */
      daddr_t blkno;
      struct ext2_inode *di;
      char buf[block_size];

      if (namelen + 2 > block_size)
	return ENAMETOOLONG;

      di = dino_ref (np->cache_id);
      blkno = di->i_translator;

      if (namelen && !blkno)
	{
	  /* Allocate block for translator */
	  blkno =
	    ext2_new_block ((diskfs_node_disknode (np)->info.i_block_group
			    * EXT2_BLOCKS_PER_GROUP (sblock))
			    + sblock->s_first_data_block,
			    0, 0, 0);
	  if (blkno == 0)
	    {
	      dino_deref (di);
	      diskfs_end_catch_exception ();
	      return ENOSPC;
	    }

	  di->i_translator = blkno;
	  diskfs_node_disknode (np)->info_i_translator = blkno;
	  record_global_poke (di);

	  np->dn_stat.st_blocks += 1 << log2_stat_blocks_per_fs_block;
	  np->dn_set_ctime = 1;
	}
      else if (!namelen && blkno)
	{
	  /* Clear block for translator going away. */
	  di->i_translator = 0;
	  diskfs_node_disknode (np)->info_i_translator = 0;
	  record_global_poke (di);
	  ext2_free_blocks (blkno, 1);

	  np->dn_stat.st_blocks -= 1 << log2_stat_blocks_per_fs_block;
	  np->dn_stat.st_mode &= ~S_IPTRANS;
	  np->dn_set_ctime = 1;
	}
      else
	dino_deref (di);

      if (namelen)
	{
	  void *blkptr;

	  buf[0] = namelen & 0xFF;
	  buf[1] = (namelen >> 8) & 0xFF;
	  memcpy (buf + 2, name, namelen);

	  blkptr = disk_cache_block_ref (blkno);
	  memcpy (blkptr, buf, block_size);
	  record_global_poke (blkptr);

	  np->dn_stat.st_mode |= S_IPTRANS;
	  np->dn_set_ctime = 1;
	}
    }

  diskfs_end_catch_exception ();
  return err;

}

/* Implement the diskfs_get_translator callback from the diskfs library.
   See <hurd/diskfs.h> for the interface description. */
error_t
diskfs_get_translator (struct node *np, char **namep, unsigned *namelen)
{
  error_t err = 0;
  daddr_t blkno;
  size_t datalen;
  void *transloc;
  struct ext2_inode *di;

  if (sblock->s_creator_os != EXT2_OS_HURD)
    return EOPNOTSUPP;

  err = diskfs_catch_exception ();
  if (err)
    return err;

  di = dino_ref (np->cache_id);
  blkno = di->i_translator;
  dino_deref (di);

  /* If an old translator record found, read it firstly */
  if (blkno)
    {
      /* If xattr is no supported by this filesystem, don't report a warning */
      if (EXT2_HAS_COMPAT_FEATURE (sblock, EXT2_FEATURE_COMPAT_EXT_ATTR)
	  && use_xattr_translator_records)
	ext2_debug ("This is an old translator record, please update it");

      transloc = disk_cache_block_ref (blkno);
      datalen =
	((unsigned char *)transloc)[0] + (((unsigned char *)transloc)[1] << 8);
      if (datalen > block_size - 2)
	err = EFTYPE;  /* ? */
      else
	{
	  *namep = malloc (datalen);
	  if (!*namep)
	    err = ENOMEM;
	  else
	    memcpy (*namep, transloc + 2, datalen);
	}

      disk_cache_block_deref (transloc);
      diskfs_end_catch_exception ();

      *namelen = datalen;
      return err;
    }

  err = ext2_get_xattr (np, "gnu.translator", NULL, &datalen);
  if (err)
    return err;

  *namep = malloc (datalen);
  if (!*namep)
    err = ENOMEM;
  else
    err = ext2_get_xattr (np, "gnu.translator", *namep, &datalen);

  diskfs_end_catch_exception ();

  *namelen = datalen;
  return err;
}

/* The maximum size of a symlink store in the inode (including '\0').  */
#define MAX_INODE_SYMLINK \
  (EXT2_N_BLOCKS * sizeof (((struct ext2_inode *)0)->i_block[0]))

/* Write an in-inode symlink, or return EINVAL if we can't.  */
static error_t
write_symlink (struct node *node, const char *target)
{
  size_t len = strlen (target) + 1;

  if (len > MAX_INODE_SYMLINK)
    return EINVAL;

  memcpy (diskfs_node_disknode (node)->info.i_data, target, len);
  node->dn_stat.st_size = len - 1;
  node->dn_set_ctime = 1;
  node->dn_set_mtime = 1;

  return 0;
}

/* Read an in-inode symlink, or return EINVAL if we can't.  */
static error_t
read_symlink (struct node *node, char *target)
{
  if (node->dn_stat.st_size >= MAX_INODE_SYMLINK)
    return EINVAL;

  memcpy (target, diskfs_node_disknode (node)->info.i_data,
          node->dn_stat.st_size);
  return 0;
}

/* If this function is nonzero (and diskfs_shortcut_symlink is set) it
   is called to set a symlink.  If it returns EINVAL or isn't set,
   then the normal method (writing the contents into the file data) is
   used.  If it returns any other error, it is returned to the user.  */
error_t (*diskfs_create_symlink_hook)(struct node *np, const char *target) =
  write_symlink;

/* If this function is nonzero (and diskfs_shortcut_symlink is set) it
   is called to read the contents of a symlink.  If it returns EINVAL or
   isn't set, then the normal method (reading from the file data) is
   used.  If it returns any other error, it is returned to the user. */
error_t (*diskfs_read_symlink_hook)(struct node *np, char *target) =
  read_symlink;

/* Called when all hard ports have gone away. */
void
diskfs_shutdown_soft_ports ()
{
  /* Should initiate termination of internally held pager ports
     (the only things that should be soft) XXX */
}
