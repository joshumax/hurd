/*
 *  Largely stolen from: linux/fs/ext2/balloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  Enhanced block allocation by Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 */

/*
 * balloc.c contains the blocks allocation and deallocation routines
 */

/*
 * The free blocks are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.  The descriptors are loaded in memory
 * when a file system is mounted (see ext2_read_super).
 */

#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/locks.h>

#include <asm/bitops.h>

#define in_range(b, first, len)		((b) >= (first) && (b) <= (first) + (len) - 1)

void 
ext2_free_blocks (unsigned long block, unsigned long count)
{
  char *bh;
  char *bh2;
  unsigned long block_group;
  unsigned long bit;
  unsigned long i;
  int bitmap_nr;
  struct ext2_group_desc *gdp;

  spin_lock (&sblock_lock);

  if (block < sblock->s_first_data_block ||
      (block + count) > sblock->s_blocks_count)
    {
      ext2_error (sb, "ext2_free_blocks",
		  "Freeing blocks not in datazone - "
		  "block = %lu, count = %lu", block, count);
      spin_unlock (&sblock_lock);
      return;
    }

  ext2_debug ("freeing block %lu\n", block);

  block_group = (block - sblock->s_first_data_block) /
    EXT2_BLOCKS_PER_GROUP (sblock);
  bit = (block - sblock->s_first_data_block) % EXT2_BLOCKS_PER_GROUP (sblock);
  if (bit + count > EXT2_BLOCKS_PER_GROUP (sblock))
    ext2_panic (sb, "ext2_free_blocks",
		"Freeing blocks across group boundary - "
		"Block = %lu, count = %lu",
		block, count);
  bitmap_nr = load_block_bitmap (sb, block_group);
  bh = sb->u.ext2_sb.s_block_bitmap[bitmap_nr];
  gdp = get_group_desc (sb, block_group, &bh2);

  if (test_opt (sb, CHECK_STRICT) &&
      (in_range (gdp->bg_block_bitmap, block, count) ||
       in_range (gdp->bg_inode_bitmap, block, count) ||
       in_range (block, gdp->bg_inode_table,
		 sb->u.ext2_sb.s_itb_per_group) ||
       in_range (block + count - 1, gdp->bg_inode_table,
		 sb->u.ext2_sb.s_itb_per_group)))
    ext2_panic (sb, "ext2_free_blocks",
		"Freeing blocks in system zones - "
		"Block = %lu, count = %lu",
		block, count);

  for (i = 0; i < count; i++)
    {
      if (!clear_bit (bit + i, bh))
	ext2_warning (sb, "ext2_free_blocks",
		      "bit already cleared for block %lu",
		      block);
      else
	{
	  gdp->bg_free_blocks_count++;
	  sblock->s_free_blocks_count++;
	}
    }

  mark_buffer_dirty (bh2, 1);
  mark_buffer_dirty (sb->u.ext2_sb.s_sbh, 1);

  mark_buffer_dirty (bh, 1);
  if (diskfs_synchronous)
    {
      ll_rw_block (WRITE, 1, &bh);
      wait_on_buffer (bh);
    }
  sblock_dirty = 1;
  spin_unlock (&sblock_lock);
  return;
}

/*
 * ext2_new_block uses a goal block to assist allocation.  If the goal is
 * free, or there is a free block within 32 blocks of the goal, that block
 * is allocated.  Otherwise a forward search is made for a free block; within 
 * each block group the search first looks for an entire free byte in the block
 * bitmap, and then for any free bit if that fails.
 */
int 
ext2_new_block (unsigned long goal,
		u32 * prealloc_count, u32 * prealloc_block)
{
  char *bh;
  char *bh2;
  char *p, *r;
  int i, j, k, tmp;
  unsigned long lmap;
  int bitmap_nr;
  struct ext2_group_desc *gdp;

#ifdef EXT2FS_DEBUG
  static int goal_hits = 0, goal_attempts = 0;
#endif

  spin_lock (&sblock_lock);

  if (sblock->s_free_blocks_count <= sblock->s_r_blocks_count &&
      (!fsuser () && (sb->u.ext2_sb.s_resuid != current->fsuid) &&
       (sb->u.ext2_sb.s_resgid == 0 ||
	!in_group_p (sb->u.ext2_sb.s_resgid))))
    {
      spin_unlock (&sblock_lock);
      return 0;
    }

  ext2_debug ("goal=%lu.\n", goal);

repeat:
  /*
     * First, test whether the goal block is free.
   */
  if (goal < sblock->s_first_data_block || goal >= sblock->s_blocks_count)
    goal = sblock->s_first_data_block;
  i = (goal - sblock->s_first_data_block) / EXT2_BLOCKS_PER_GROUP (sblock);
  gdp = get_group_desc (sb, i, &bh2);
  if (gdp->bg_free_blocks_count > 0)
    {
      j = ((goal - sblock->s_first_data_block) % EXT2_BLOCKS_PER_GROUP (sblock));
#ifdef EXT2FS_DEBUG
      if (j)
	goal_attempts++;
#endif
      bitmap_nr = load_block_bitmap (sb, i);
      bh = sb->u.ext2_sb.s_block_bitmap[bitmap_nr];

      ext2_debug ("goal is at %d:%d.\n", i, j);

      if (!test_bit (j, bh))
	{
#ifdef EXT2FS_DEBUG
	  goal_hits++;
	  ext2_debug ("goal bit allocated.\n");
#endif
	  goto got_block;
	}
      if (j)
	{
	  /*
	     * The goal was occupied; search forward for a free 
	     * block within the next 32 blocks
	   */
	  lmap = ((((unsigned long *) bh)[j >> 5]) >>
		  ((j & 31) + 1));
	  if (j < EXT2_BLOCKS_PER_GROUP (sblock) - 32)
	    lmap |= (((unsigned long *) bh)[(j >> 5) + 1]) <<
	      (31 - (j & 31));
	  else
	    lmap |= 0xffffffff << (31 - (j & 31));
	  if (lmap != 0xffffffffl)
	    {
	      k = ffz (lmap) + 1;
	      if ((j + k) < EXT2_BLOCKS_PER_GROUP (sblock))
		{
		  j += k;
		  goto got_block;
		}
	    }
	}

      ext2_debug ("Bit not found near goal\n");

      /*
         * There has been no free block found in the near vicinity
         * of the goal: do a search forward through the block groups,
         * searching in each group first for an entire free byte in
         * the bitmap and then for any free bit.
         * 
         * Search first in the remainder of the current group; then,
         * cyclicly search through the rest of the groups.
       */
      p = ((char *) bh) + (j >> 3);
      r = memscan (p, 0, (EXT2_BLOCKS_PER_GROUP (sblock) - j + 7) >> 3);
      k = (r - ((char *) bh)) << 3;
      if (k < EXT2_BLOCKS_PER_GROUP (sblock))
	{
	  j = k;
	  goto search_back;
	}
      k = find_next_zero_bit ((unsigned long *) bh,
			      EXT2_BLOCKS_PER_GROUP (sblock),
			      j);
      if (k < EXT2_BLOCKS_PER_GROUP (sblock))
	{
	  j = k;
	  goto got_block;
	}
    }

  ext2_debug ("Bit not found in block group %d.\n", i);

  /*
     * Now search the rest of the groups.  We assume that 
     * i and gdp correctly point to the last group visited.
   */
  for (k = 0; k < sb->u.ext2_sb.s_groups_count; k++)
    {
      i++;
      if (i >= sb->u.ext2_sb.s_groups_count)
	i = 0;
      gdp = get_group_desc (sb, i, &bh2);
      if (gdp->bg_free_blocks_count > 0)
	break;
    }
  if (k >= sb->u.ext2_sb.s_groups_count)
    {
      spin_unlock (&sblock_lock);
      return 0;
    }
  bitmap_nr = load_block_bitmap (sb, i);
  bh = sb->u.ext2_sb.s_block_bitmap[bitmap_nr];
  r = memscan (bh, 0, EXT2_BLOCKS_PER_GROUP (sblock) >> 3);
  j = (r - bh) << 3;
  if (j < EXT2_BLOCKS_PER_GROUP (sblock))
    goto search_back;
  else
    j = find_first_zero_bit ((unsigned long *) bh,
			     EXT2_BLOCKS_PER_GROUP (sblock));
  if (j >= EXT2_BLOCKS_PER_GROUP (sblock))
    {
      ext2_error (sb, "ext2_new_block",
		  "Free blocks count corrupted for block group %d", i);
      spin_unlock (&sblock_lock);
      return 0;
    }

search_back:
  /* 
     * We have succeeded in finding a free byte in the block
     * bitmap.  Now search backwards up to 7 bits to find the
     * start of this group of free blocks.
   */
  for (k = 0; k < 7 && j > 0 && !test_bit (j - 1, bh); k++, j--);

got_block:

  ext2_debug ("using block group %d(%d)\n", i, gdp->bg_free_blocks_count);

  tmp = j + i * EXT2_BLOCKS_PER_GROUP (sblock) + sblock->s_first_data_block;

  if (test_opt (sb, CHECK_STRICT) &&
      (tmp == gdp->bg_block_bitmap ||
       tmp == gdp->bg_inode_bitmap ||
       in_range (tmp, gdp->bg_inode_table, sb->u.ext2_sb.s_itb_per_group)))
    ext2_panic (sb, "ext2_new_block",
		"Allocating block in system zone - "
		"block = %u", tmp);

  if (set_bit (j, bh))
    {
      ext2_warning (sb, "ext2_new_block",
		    "bit already set for block %d", j);
      goto repeat;
    }

  ext2_debug ("found bit %d\n", j);

  /*
     * Do block preallocation now if required.
   */
#ifdef EXT2_PREALLOCATE
  if (prealloc_block)
    {
      *prealloc_count = 0;
      *prealloc_block = tmp + 1;
      for (k = 1;
	   k < 8 && (j + k) < EXT2_BLOCKS_PER_GROUP (sblock); k++)
	{
	  if (set_bit (j + k, bh))
	    break;
	  (*prealloc_count)++;
	}
      gdp->bg_free_blocks_count -= *prealloc_count;
      sblock->s_free_blocks_count -= *prealloc_count;
      ext2_debug ("Preallocated a further %lu bits.\n",
		  *prealloc_count);
    }
#endif

  j = tmp;

  mark_buffer_dirty (bh, 1);
  if (diskfs_synchronous)
    {
      ll_rw_block (WRITE, 1, &bh);
      wait_on_buffer (bh);
    }

  if (j >= sblock->s_blocks_count)
    {
      ext2_error (sb, "ext2_new_block",
		  "block >= blocks count - "
		  "block_group = %d, block=%d", i, j);
      spin_unlock (&sblock_lock);
      return 0;
    }
  bh = baddr (j);
  memset (bh, 0, block_size);
  brelse (bh);

  ext2_debug ("allocating block %d. "
	      "Goal hits %d of %d.\n", j, goal_hits, goal_attempts);

  gdp->bg_free_blocks_count--;
  mark_buffer_dirty (bh2, 1);
  sblock->s_free_blocks_count--;
  mark_buffer_dirty (sb->u.ext2_sb.s_sbh, 1);
  sblock_dirty = 1;
  spin_unlock (&sblock_lock);
  return j;
}

unsigned long 
ext2_count_free_blocks ()
{
#ifdef EXT2FS_DEBUG
  unsigned long desc_count, bitmap_count, x;
  int bitmap_nr;
  struct ext2_group_desc *gdp;
  int i;

  spin_lock (&sblock_lock);

  desc_count = 0;
  bitmap_count = 0;
  gdp = NULL;
  for (i = 0; i < sb->u.ext2_sb.s_groups_count; i++)
    {
      gdp = get_group_desc (sb, i, NULL);
      desc_count += gdp->bg_free_blocks_count;
      bitmap_nr = load_block_bitmap (sb, i);
      x = ext2_count_free (sb->u.ext2_sb.s_block_bitmap[bitmap_nr],
			   block_size);
      printk ("group %d: stored = %d, counted = %lu\n",
	      i, gdp->bg_free_blocks_count, x);
      bitmap_count += x;
    }
  printk ("ext2_count_free_blocks: stored = %lu, computed = %lu, %lu\n",
	  sblock->s_free_blocks_count, desc_count, bitmap_count);
  spin_unlock (&sblock_lock);
  return bitmap_count;
#else
  return sb->u.ext2_sb.s_sblock->s_free_blocks_count;
#endif
}

static inline int 
block_in_use (unsigned long block, unsigned char *map)
{
  return test_bit ((block - sb->u.ext2_sb.s_sblock->s_first_data_block) %
		   EXT2_BLOCKS_PER_GROUP (sblock), map);
}

void 
ext2_check_blocks_bitmap ()
{
  char *bh;
  unsigned long desc_count, bitmap_count, x;
  unsigned long desc_blocks;
  int bitmap_nr;
  struct ext2_group_desc *gdp;
  int i, j;

  spin_lock (&sblock_lock);

  desc_count = 0;
  bitmap_count = 0;
  gdp = NULL;
  desc_blocks = (sb->u.ext2_sb.s_groups_count + EXT2_DESC_PER_BLOCK (sblock) - 1) /
    EXT2_DESC_PER_BLOCK (sblock);
  for (i = 0; i < sb->u.ext2_sb.s_groups_count; i++)
    {
      gdp = get_group_desc (sb, i, NULL);
      desc_count += gdp->bg_free_blocks_count;
      bitmap_nr = load_block_bitmap (sb, i);
      bh = sb->u.ext2_sb.s_block_bitmap[bitmap_nr];

      if (!test_bit (0, bh))
	ext2_error (sb, "ext2_check_blocks_bitmap",
		    "Superblock in group %d is marked free", i);

      for (j = 0; j < desc_blocks; j++)
	if (!test_bit (j + 1, bh))
	  ext2_error (sb, "ext2_check_blocks_bitmap",
		      "Descriptor block #%d in group "
		      "%d is marked free", j, i);

      if (!block_in_use (gdp->bg_block_bitmap, sb, bh))
	ext2_error (sb, "ext2_check_blocks_bitmap",
		    "Block bitmap for group %d is marked free",
		    i);

      if (!block_in_use (gdp->bg_inode_bitmap, sb, bh))
	ext2_error (sb, "ext2_check_blocks_bitmap",
		    "Inode bitmap for group %d is marked free",
		    i);

      for (j = 0; j < sb->u.ext2_sb.s_itb_per_group; j++)
	if (!block_in_use (gdp->bg_inode_table + j, sb, bh))
	  ext2_error (sb, "ext2_check_blocks_bitmap",
		      "Block #%d of the inode table in "
		      "group %d is marked free", j, i);

      x = ext2_count_free (bh, block_size);
      if (gdp->bg_free_blocks_count != x)
	ext2_error (sb, "ext2_check_blocks_bitmap",
		    "Wrong free blocks count for group %d, "
		    "stored = %d, counted = %lu", i,
		    gdp->bg_free_blocks_count, x);
      bitmap_count += x;
    }
  if (sblock->s_free_blocks_count != bitmap_count)
    ext2_error (sb, "ext2_check_blocks_bitmap",
		"Wrong free blocks count in super block, "
		"stored = %lu, counted = %lu",
		(unsigned long) sblock->s_free_blocks_count, bitmap_count);
  spin_unlock (&sblock_lock);
}
