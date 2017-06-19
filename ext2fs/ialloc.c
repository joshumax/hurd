/* Inode allocation routines.

   Copyright (C) 1995,96,99,2000,02 Free Software Foundation, Inc.

   Converted to work under the hurd by Miles Bader <miles@gnu.org>

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

/*
 *  linux/fs/ext2/ialloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  BSD ufs-inspired inode and directory allocation by
 *  Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 */

/*
 * The free inodes are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.  The descriptors are loaded in memory
 * when a file system is mounted (see ext2_read_super).
 */

#include "ext2fs.h"
#include "bitmap.c"

/* ---------------------------------------------------------------- */

/* Free node NP; the on disk copy has already been synced with
   diskfs_node_update (where NP->dn_stat.st_mode was 0).  It's
   mode used to be OLD_MODE.  */
void
diskfs_free_node (struct node *np, mode_t old_mode)
{
  unsigned char *bh;
  unsigned long block_group;
  unsigned long bit;
  struct ext2_group_desc *gdp;
  ino_t inum = np->cache_id;

  assert_backtrace (!diskfs_readonly);

  ext2_debug ("freeing inode %u", inum);

  ext2_free_xattr_block (np);

  pthread_spin_lock (&global_lock);

  if (inum < EXT2_FIRST_INO (sblock) || inum > sblock->s_inodes_count)
    {
      ext2_error ("reserved inode or nonexistent inode: %Ld", inum);
      pthread_spin_unlock (&global_lock);
      return;
    }

  block_group = (inum - 1) / sblock->s_inodes_per_group;
  bit = (inum - 1) % sblock->s_inodes_per_group;

  gdp = group_desc (block_group);
  bh = disk_cache_block_ref (gdp->bg_inode_bitmap);

  if (!clear_bit (bit, bh))
    ext2_warning ("bit already cleared for inode %Ld", inum);
  else
    {
      disk_cache_block_ref_ptr (bh);
      record_global_poke (bh);

      gdp->bg_free_inodes_count++;
      if (S_ISDIR (old_mode))
	gdp->bg_used_dirs_count--;
      disk_cache_block_ref_ptr (gdp);
      record_global_poke (gdp);

      sblock->s_free_inodes_count++;
    }

  disk_cache_block_deref (bh);
  sblock_dirty = 1;
  pthread_spin_unlock (&global_lock);
  alloc_sync(0);
}

/* ---------------------------------------------------------------- */

/*
 * There are two policies for allocating an inode.  If the new inode is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-inode ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other inodes, search forward from the parent directory\'s block
 * group to find a free inode.
 */
ino_t
ext2_alloc_inode (ino_t dir_inum, mode_t mode)
{
  unsigned char *bh = NULL;
  int i, j, avefreei;
  ino_t inum;
  struct ext2_group_desc *gdp;
  struct ext2_group_desc *tmp;

  pthread_spin_lock (&global_lock);

repeat:
  assert_backtrace (bh == NULL);
  gdp = NULL;
  i = 0;

  if (S_ISDIR (mode))
    {
      avefreei = sblock->s_free_inodes_count / groups_count;

/* I am not yet convinced that this next bit is necessary.
      i = inode_group_num(dir_inum);
      for (j = 0; j < groups_count; j++)
	{
	  tmp = group_desc (i);
	  if ((tmp->bg_used_dirs_count << 8) < tmp->bg_free_inodes_count)
	    {
	      gdp = tmp;
	      break;
	    }
	  else
	    i = ++i % groups_count;
	}
 */

      if (!gdp)
	{
	  for (j = 0; j < groups_count; j++)
	    {
	      tmp = group_desc (j);
	      if (tmp->bg_free_inodes_count
		  && tmp->bg_free_inodes_count >= avefreei)
		{
		  if (!gdp ||
		      (tmp->bg_free_blocks_count > gdp->bg_free_blocks_count))
		    {
		      i = j;
		      gdp = tmp;
		    }
		}
	    }
	}
    }
  else
    {
      /*
       * Try to place the inode in its parent directory
       */
      i = inode_group_num(dir_inum);
      tmp = group_desc (i);
      if (tmp->bg_free_inodes_count)
	gdp = tmp;
      else
	{
	  /*
	   * Use a quadratic hash to find a group with a
	   * free inode
	   */
	  for (j = 1; j < groups_count; j <<= 1)
	    {
	      i += j;
	      if (i >= groups_count)
		i -= groups_count;
	      tmp = group_desc (i);
	      if (tmp->bg_free_inodes_count)
		{
		  gdp = tmp;
		  break;
		}
	    }
	}
      if (!gdp)
	{
	  /*
	   * That failed: try linear search for a free inode
	   */
	  i = inode_group_num(dir_inum) + 1;
	  for (j = 2; j < groups_count; j++)
	    {
	      if (++i >= groups_count)
		i = 0;
	      tmp = group_desc (i);
	      if (tmp->bg_free_inodes_count)
		{
		  gdp = tmp;
		  break;
		}
	    }
	}
    }

  if (!gdp)
    {
      pthread_spin_unlock (&global_lock);
      return 0;
    }

  bh = disk_cache_block_ref (gdp->bg_inode_bitmap);
  if ((inum =
       find_first_zero_bit ((unsigned long *) bh, sblock->s_inodes_per_group))
      < sblock->s_inodes_per_group)
    {
      if (set_bit (inum, bh))
	{
	  ext2_warning ("bit already set for inode %llu", inum);
	  disk_cache_block_deref (bh);
	  bh = NULL;
	  goto repeat;
	}
      record_global_poke (bh);
      bh = NULL;
    }
  else
    {
      disk_cache_block_deref (bh);
      bh = NULL;
      if (gdp->bg_free_inodes_count != 0)
	{
	  ext2_error ("free inodes count corrupted in group %d", i);
	  inum = 0;
	  goto sync_out;
	}
      goto repeat;
    }

  inum += i * sblock->s_inodes_per_group + 1;
  if (inum < EXT2_FIRST_INO (sblock) || inum > sblock->s_inodes_count)
    {
      ext2_error ("reserved inode or inode > inodes count - "
		  "block_group = %d,inode=%llu", i, inum);
      inum = 0;
      goto sync_out;
    }

  gdp->bg_free_inodes_count--;
  if (S_ISDIR (mode))
    gdp->bg_used_dirs_count++;
  disk_cache_block_ref_ptr (gdp);
  record_global_poke (gdp);

  sblock->s_free_inodes_count--;
  sblock_dirty = 1;

 sync_out:
  assert_backtrace (bh == NULL);
  pthread_spin_unlock (&global_lock);
  alloc_sync (0);

  /* Make sure the coming read_node won't complain about bad
     fields.  */
  {
    struct ext2_inode *di = dino_ref (inum);
    memset (di, 0, sizeof *di);
    dino_deref (di);
  }

  return inum;
}

/* ---------------------------------------------------------------- */

/* The user must define this function.  Allocate a new node to be of
   mode MODE in locked directory DP (don't actually set the mode or
   modify the dir, that will be done by the caller); the user
   responsible for the request can be identified with CRED.  Set *NP
   to be the newly allocated node.  */
error_t
diskfs_alloc_node (struct node *dir, mode_t mode, struct node **node)
{
  error_t err;
  int sex, block;
  struct node *np;
  struct stat *st;
  ino_t inum;

  assert_backtrace (!diskfs_readonly);

  inum = ext2_alloc_inode (dir->cache_id, mode);

  if (inum == 0)
    return ENOSPC;

  err = diskfs_cached_lookup (inum, &np);
  if (err)
    return err;

  st = &np->dn_stat;

  if (st->st_blocks)
    {
      st->st_blocks = 0;
      np->dn_set_ctime = 1;
    }
  /* Zero out the block pointers in case there's some noise left on disk.  */
  for (block = 0; block < EXT2_N_BLOCKS; block++)
    if (diskfs_node_disknode (np)->info.i_data[block] != 0)
      {
	diskfs_node_disknode (np)->info.i_data[block] = 0;
	np->dn_set_ctime = 1;
      }
  if (diskfs_node_disknode (np)->info_i_translator != 0)
    {
      diskfs_node_disknode (np)->info_i_translator = 0;
      np->dn_set_ctime = 1;
    }
  st->st_mode &= ~S_IPTRANS;
  if (np->allocsize)
    {
      st->st_size = 0;
      np->allocsize = 0;
      np->dn_set_ctime = 1;
    }

  /* Propagate initial inode flags from the directory, as Linux does.  */
  diskfs_node_disknode (np)->info.i_flags =
    ext2_mask_flags(mode,
	       diskfs_node_disknode (dir)->info.i_flags & EXT2_FL_INHERITED);

  st->st_flags = 0;

  /*
   * Set up a new generation number for this inode.
   */
  pthread_spin_lock (&generation_lock);
  sex = diskfs_mtime->seconds;
  if (++next_generation < (u_long)sex)
    next_generation = sex;
  st->st_gen = next_generation;
  pthread_spin_unlock (&generation_lock);

  alloc_sync (np);

  *node = np;
  return 0;
}

/* ---------------------------------------------------------------- */

unsigned long
ext2_count_free_inodes ()
{
#ifdef EXT2FS_DEBUG
  unsigned long desc_count, bitmap_count, x;
  struct ext2_group_desc *gdp;
  int i;

  pthread_spin_lock (&global_lock);

  desc_count = 0;
  bitmap_count = 0;
  gdp = NULL;
  for (i = 0; i < groups_count; i++)
    {
      void *bh;
      gdp = group_desc (i);
      desc_count += gdp->bg_free_inodes_count;
      bh = disk_cache_block_ref (gdp->bg_inode_bitmap);
      x = count_free (bh, sblock->s_inodes_per_group / 8);
      disk_cache_block_deref (bh);
      ext2_debug ("group %d: stored = %d, counted = %lu",
		  i, gdp->bg_free_inodes_count, x);
      bitmap_count += x;
    }
  ext2_debug ("stored = %u, computed = %lu, %lu",
	      sblock->s_free_inodes_count, desc_count, bitmap_count);
  pthread_spin_unlock (&global_lock);
  return desc_count;
#else
  return sblock->s_free_inodes_count;
#endif
}

/* ---------------------------------------------------------------- */

void
ext2_check_inodes_bitmap ()
{
  int i;
  struct ext2_group_desc *gdp;
  unsigned long desc_count, bitmap_count, x;

  pthread_spin_lock (&global_lock);

  desc_count = 0;
  bitmap_count = 0;
  gdp = NULL;
  for (i = 0; i < groups_count; i++)
    {
      void *bh;
      gdp = group_desc (i);
      desc_count += gdp->bg_free_inodes_count;
      bh = disk_cache_block_ref (gdp->bg_inode_bitmap);
      x = count_free (bh, sblock->s_inodes_per_group / 8);
      disk_cache_block_deref (bh);
      if (gdp->bg_free_inodes_count != x)
	ext2_error ("wrong free inodes count in group %d, "
		    "stored = %d, counted = %lu",
		    i, gdp->bg_free_inodes_count, x);
      bitmap_count += x;
    }
  if (sblock->s_free_inodes_count != bitmap_count)
    ext2_error ("wrong free inodes count in super block, "
		"stored = %lu, counted = %lu",
		(unsigned long) sblock->s_free_inodes_count, bitmap_count);

  pthread_spin_unlock (&global_lock);
}
