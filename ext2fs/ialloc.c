/* Inode allocation routines.
 *
 * Converted to work under the hurd by Miles Bader <miles@gnu.ai.mit.edu>
 *
 * Largely stolen from linux/fs/ext2/ialloc.c
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

/* ---------------------------------------------------------------- */

void 
ext2_free_inode (struct node *node, mode_t old_mode)
{
  char *bh;
  unsigned long block_group;
  unsigned long bit;
  struct ext2_group_desc *gdp;
  ino_t inum = node->dn->number;

  ext2_debug ("freeing inode %lu\n", inum);

  spin_lock (&sblock_lock);

  if (inum < EXT2_FIRST_INO || inum > sblock->s_inodes_count)
    {
      ext2_error ("free_inode", "reserved inode or nonexistent inode");
      spin_unlock (&sblock_lock);
      return;
    }

  block_group = (inum - 1) / sblock->s_inodes_per_group;
  bit = (inum - 1) % sblock->s_inodes_per_group;

  gdp = group_desc (glock_group);
  bh = baddr (gdb->bg_inode_bitmap);

  if (!clear_bit (bit, bh))
    ext2_warning ("ext2_free_inode", "bit already cleared for inode %u", inum);
  else
    {
      record_poke (bh, block_size);

      gdp->bg_free_inodes_count++;
      if (S_ISDIR (old_mode))
	gdp->bg_used_dirs_count--;
      record_poke (gdp, sizeof *gdp);

      sblock->s_free_inodes_count++;
    }

  sblock_dirty = 1;
  spin_unlock (&sblock_lock);
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
  char *bh;
  int i, j, inum, avefreei;
  struct ext2_group_desc *gdp;
  struct ext2_group_desc *tmp;

  spin_lock (&sblock_lock);

repeat:
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
      spin_unlock (&sblock_lock);
      return 0;
    }

  bh = gdp->bg_inode_bitmap;
  if ((inum =
       find_first_zero_bit ((unsigned long *) bh, sblock->s_inodes_per_group))
      < sblock->s_inodes_per_group)
    {
      if (set_bit (inum, bh))
	{
	  ext2_warning ("ext2_new_inode",
			"bit already set for inode %d", inum);
	  goto repeat;
	}
      record_poke (bh, block_size);
    }
  else
    {
      if (gdp->bg_free_inodes_count != 0)
	{
	  ext2_error ("ext2_new_inode",
		      "Free inodes count corrupted in group %d", i);
	  inum = 0;
	  goto sync_out;
	}
      goto repeat;
    }

  inum += i * sblock->s_inodes_per_group + 1;
  if (inum < EXT2_FIRST_INO || inum > sblock->s_inodes_count)
    {
      ext2_error ("ext2_new_inode",
		  "reserved inode or inode > inodes count - "
		  "block_group = %d,inode=%d", i, inum);
      inum = 0;
      goto sync_out;
    }

  gdp->bg_free_inodes_count--;
  if (S_ISDIR (mode))
    gdp->bg_used_dirs_count++;
  record_poke (gdp, sizeof *gdp);

  sblock->s_free_inodes_count--;
  sblock_dirty = 1;

 sync_out:
  spin_unlock (&sblock_lock);
  alloc_sync (0);

  return inum;
}

/* ---------------------------------------------------------------- */

error_t
diskfs_alloc_node (const struct node *dir, int mode, struct node **node)
{
  int sex;
  struct node *np;
  ino_t inum = ext2_alloc_inode (dir->dn->number, mode);

  if (inum == 0)
    return ENOSPC;

  err = iget (inum, &np);
  if (err)
    return err;

  if (np->dn_stat.st_mode)
    ext2_panic("Duplicate inode: %d", inum);

  if (np->dn_stat.st_blocks)
    {
      ext2_warning("Free inode %d had %d blocks", inum, np->dn_stat.st_blocks);
      np->dn_stat.st_blocks = 0;
      np->dn_set_ctime = 1;
    }

  np->dn_stat.st_flags = 0;

  /*
   * Set up a new generation number for this inode.
   */
  spin_lock (&gennumberlock);
  sex = diskfs_mtime->seconds;
  if (++nextgennumber < (u_long)sex)
    nextgennumber = sex;
  np->dn_stat.st_gen = nextgennumber;
  spin_unlock (&gennumberlock);

  alloc_sync (np);

  *node = np;
  return 0;
}

/* ---------------------------------------------------------------- */

unsigned long 
ext2_count_free_inodes (struct super_block *sb)
{
#ifdef EXT2FS_DEBUG
  struct ext2_super_block *es;
  unsigned long desc_count, bitmap_count, x;
  int bitmap_nr;
  struct ext2_group_desc *gdp;
  int i;

  spin_lock (&sblock_lock);

  desc_count = 0;
  bitmap_count = 0;
  gdp = NULL;
  for (i = 0; i < groups_count; i++)
    {
      gdp = group_desc (i);
      desc_count += gdp->bg_free_inodes_count;
      x = ext2_count_free (gdp->bg_inode_bitmap, sblock->s_inodes_per_group / 8);
      printf ("group %d: stored = %d, counted = %lu\n", i, gdp->bg_free_inodes_count, x);
      bitmap_count += x;
    }
  printf ("ext2_count_free_inodes: stored = %lu, computed = %lu, %lu\n",
	  sblock->s_free_inodes_count, desc_count, bitmap_count);
  spin_unlock (&sblock_lock);
  return desc_count;
#else
  return sblock->s_free_inodes_count;
#endif
}

/* ---------------------------------------------------------------- */

void 
ext2_check_inodes_bitmap (struct super_block *sb)
{
  int i;
  struct ext2_group_desc *gdp;
  unsigned long desc_count, bitmap_count, x;

  spin_lock (&sblock_lock);

  desc_count = 0;
  bitmap_count = 0;
  gdp = NULL;
  for (i = 0; i < groups_count; i++)
    {
      gdp = get_group_desc (sb, i, NULL);
      desc_count += gdp->bg_free_inodes_count;
      x = ext2_count_free (gdp->bg_inode_bitmap, sblock->s_inodes_per_group / 8);
      if (gdp->bg_free_inodes_count != x)
	ext2_error ("ext2_check_inodes_bitmap",
		    "Wrong free inodes count in group %d, "
		    "stored = %d, counted = %lu", i,
		    gdp->bg_free_inodes_count, x);
      bitmap_count += x;
    }
  if (sblock->s_free_inodes_count != bitmap_count)
    ext2_error ("ext2_check_inodes_bitmap",
		"Wrong free inodes count in super block, "
		"stored = %lu, counted = %lu",
		(unsigned long) sblock->s_free_inodes_count, bitmap_count);

  spin_unlock (&sblock_lock);
}
