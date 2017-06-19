/* Block allocation routines

   Copyright (C) 1995,99,2000 Free Software Foundation, Inc.

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
 *  linux/fs/ext2/balloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  Enhanced block allocation by Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
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

#include <string.h>
#include "ext2fs.h"
#include "bitmap.c"

/* Returns a pointer to the first occurrence of CH in the buffer BUF of len
   LEN, or BUF + LEN if CH doesn't occur.  */
static inline void *
memscan (void *buf, unsigned char ch, size_t len)
{
  return memchr (buf, ch, len) ?: buf + len;
}

#define in_range(b, first, len) ((b) >= (first) && (b) <= (first) + (len) - 1)

void
ext2_free_blocks (block_t block, unsigned long count)
{
  unsigned char *bh;
  unsigned long block_group;
  unsigned long bit;
  unsigned long i;
  struct ext2_group_desc *gdp;

  pthread_spin_lock (&global_lock);

  if (block < sblock->s_first_data_block ||
      (block + count) > sblock->s_blocks_count)
    {
      ext2_error ("freeing blocks not in datazone - "
		  "block = %u, count = %lu", block, count);
      pthread_spin_unlock (&global_lock);
      return;
    }

  ext2_debug ("freeing block %u[%lu]", block, count);

  do
    {
      unsigned long int gcount = count;

      block_group = ((block - sblock->s_first_data_block)
		     / sblock->s_blocks_per_group);
      bit = (block - sblock->s_first_data_block) % sblock->s_blocks_per_group;
      if (bit + count > sblock->s_blocks_per_group)
	{
	  unsigned long overflow = bit + count - sblock->s_blocks_per_group;
	  gcount -= overflow;
	  ext2_debug ("freeing blocks across group boundary - "
		      "block = %u, count = %lu",
		      block, count);
	}
      gdp = group_desc (block_group);
      bh = disk_cache_block_ref (gdp->bg_block_bitmap);

      if (in_range (gdp->bg_block_bitmap, block, gcount) ||
	  in_range (gdp->bg_inode_bitmap, block, gcount) ||
	  in_range (block, gdp->bg_inode_table, itb_per_group) ||
	  in_range (block + gcount - 1, gdp->bg_inode_table, itb_per_group))
	ext2_panic ("freeing blocks in system zones - "
		    "block = %u, count = %lu",
		    block, count);

      for (i = 0; i < gcount; i++)
	{
	  if (!clear_bit (bit + i, bh))
	    ext2_warning ("bit already cleared for block %lu", block + i);
	  else
	    {
	      gdp->bg_free_blocks_count++;
	      sblock->s_free_blocks_count++;
	    }
	}

      record_global_poke (bh);
      disk_cache_block_ref_ptr (gdp);
      record_global_poke (gdp);

      block += gcount;
      count -= gcount;
    } while (count > 0);

  sblock_dirty = 1;

  pthread_spin_unlock (&global_lock);

  alloc_sync (0);
}

/*
 * ext2_new_block uses a goal block to assist allocation.  If the goal is
 * free, or there is a free block within 32 blocks of the goal, that block
 * is allocated.  Otherwise a forward search is made for a free block; within
 * each block group the search first looks for an entire free byte in the block
 * bitmap, and then for any free bit if that fails.
 */
block_t
ext2_new_block (block_t goal,
		block_t prealloc_goal,
		block_t *prealloc_count, block_t *prealloc_block)
{
  unsigned char *bh = NULL;
  unsigned char *p, *r;
  int i, j, k, tmp;
  unsigned long lmap;
  struct ext2_group_desc *gdp;

#ifdef EXT2FS_DEBUG
  static int goal_hits = 0, goal_attempts = 0;
#endif

  pthread_spin_lock (&global_lock);

#ifdef XXX /* Auth check to use reserved blocks  */
  if (sblock->s_free_blocks_count <= sblock->s_r_blocks_count &&
      (!fsuser () && (sb->u.ext2_sb.s_resuid != current->fsuid) &&
       (sb->u.ext2_sb.s_resgid == 0 ||
	!in_group_p (sb->u.ext2_sb.s_resgid))))
    {
      pthread_spin_unlock (&global_lock);
      return 0;
    }
#endif

  ext2_debug ("goal=%u", goal);

repeat:
  assert_backtrace (bh == NULL);
  /*
     * First, test whether the goal block is free.
   */
  if (goal < sblock->s_first_data_block || goal >= sblock->s_blocks_count)
    goal = sblock->s_first_data_block;
  i = (goal - sblock->s_first_data_block) / sblock->s_blocks_per_group;
  gdp = group_desc (i);
  if (gdp->bg_free_blocks_count > 0)
    {
      j = ((goal - sblock->s_first_data_block) % sblock->s_blocks_per_group);
#ifdef EXT2FS_DEBUG
      if (j)
	goal_attempts++;
#endif
      bh = disk_cache_block_ref (gdp->bg_block_bitmap);

      ext2_debug ("goal is at %d:%d", i, j);

      if (!test_bit (j, bh))
	{
#ifdef EXT2FS_DEBUG
	  goal_hits++;
	  ext2_debug ("goal bit allocated!");
#endif
	  goto got_block;
	}
      if (j)
	{
	  /*
	     * The goal was occupied; search forward for a free
	     * block within the next 32 blocks
	   */
	  if ((j & 31) == 31)
	    lmap = 0;
	  else
	    lmap = ((((unsigned long *) bh)[j >> 5]) >>
		    ((j & 31) + 1));
	  if (j < sblock->s_blocks_per_group - 32)
	    lmap |= (((unsigned long *) bh)[(j >> 5) + 1]) <<
	      (31 - (j & 31));
	  else
	    lmap |= 0xffffffff << (31 - (j & 31));
	  if (lmap != 0xffffffffl)
	    {
	      k = ffz (lmap) + 1;
	      if ((j + k) < sblock->s_blocks_per_group)
		{
		  j += k;
		  goto got_block;
		}
	    }
	}

      ext2_debug ("bit not found near goal");

      /*
       * There has been no free block found in the near vicinity
       * of the goal: do a search forward through the block groups,
       * searching in each group first for an entire free byte in
       * the bitmap and then for any free bit.
       *
       * Search first in the remainder of the current group; then,
       * cyclicly search through the rest of the groups.
       */
      p = bh + (j >> 3);
      r = memscan (p, 0, (sblock->s_blocks_per_group - j + 7) >> 3);
      k = (r - bh) << 3;
      if (k < sblock->s_blocks_per_group)
	{
	  j = k;
	  goto search_back;
	}
      k = find_next_zero_bit ((unsigned long *) bh,
			      sblock->s_blocks_per_group,
			      j);
      if (k < sblock->s_blocks_per_group)
	{
	  j = k;
	  goto got_block;
	}

      disk_cache_block_deref (bh);
      bh = NULL;
    }

  ext2_debug ("bit not found in block group %d", i);

  /*
     * Now search the rest of the groups.  We assume that
     * i and gdp correctly point to the last group visited.
   */
  for (k = 0; k < groups_count; k++)
    {
      i++;
      if (i >= groups_count)
	i = 0;
      gdp = group_desc (i);
      if (gdp->bg_free_blocks_count > 0)
	break;
    }
  if (k >= groups_count)
    {
      pthread_spin_unlock (&global_lock);
      return 0;
    }
  assert_backtrace (bh == NULL);
  bh = disk_cache_block_ref (gdp->bg_block_bitmap);
  r = memscan (bh, 0, sblock->s_blocks_per_group >> 3);
  j = (r - bh) << 3;
  if (j < sblock->s_blocks_per_group)
    goto search_back;
  else
    j = find_first_zero_bit ((unsigned long *) bh,
			     sblock->s_blocks_per_group);
  if (j >= sblock->s_blocks_per_group)
    {
      disk_cache_block_deref (bh);
      bh = NULL;
      ext2_error ("free blocks count corrupted for block group %d", i);
      pthread_spin_unlock (&global_lock);
      return 0;
    }

search_back:
  assert_backtrace (bh != NULL);
  /*
     * We have succeeded in finding a free byte in the block
     * bitmap.  Now search backwards up to 7 bits to find the
     * start of this group of free blocks.
   */
  for (k = 0; k < 7 && j > 0 && !test_bit (j - 1, bh); k++, j--);

got_block:
  assert_backtrace (bh != NULL);

  ext2_debug ("using block group %d (%d)", i, gdp->bg_free_blocks_count);

  tmp = j + i * sblock->s_blocks_per_group + sblock->s_first_data_block;

  if (tmp == gdp->bg_block_bitmap ||
      tmp == gdp->bg_inode_bitmap ||
      in_range (tmp, gdp->bg_inode_table, itb_per_group))
    ext2_panic ("allocating block in system zone; block = %u", tmp);

  if (set_bit (j, bh))
    {
      ext2_warning ("bit already set for block %d", j);
      disk_cache_block_deref (bh);
      bh = NULL;
      goto repeat;
    }

  /* Since due to bletcherousness block-modified bits are never turned off
     when writing disk-pager pages, make sure they are here, in case this
     block is being allocated to a file (see pager.c).  */
  if (modified_global_blocks)
    {
      pthread_spin_lock (&modified_global_blocks_lock);
      clear_bit (tmp, modified_global_blocks);
      pthread_spin_unlock (&modified_global_blocks_lock);
    }

  ext2_debug ("found bit %d", j);

  /*
     * Do block preallocation now if required.
   */
#ifdef EXT2_PREALLOCATE
  if (prealloc_goal)
    {
      *prealloc_count = 0;
      *prealloc_block = tmp + 1;
      for (k = 1;
	   k < prealloc_goal && (j + k) < sblock->s_blocks_per_group; k++)
	{
	  if (set_bit (j + k, bh))
	    break;
	  (*prealloc_count)++;

	  /* (See comment before the clear_bit above) */
	  if (modified_global_blocks)
	    {
	      pthread_spin_lock (&modified_global_blocks_lock);
	      clear_bit (tmp + k, modified_global_blocks);
	      pthread_spin_unlock (&modified_global_blocks_lock);
	    }
	}
      gdp->bg_free_blocks_count -= *prealloc_count;
      sblock->s_free_blocks_count -= *prealloc_count;
      ext2_debug ("preallocated a further %u bits", *prealloc_count);
    }
#endif

  j = tmp;

  record_global_poke (bh);
  bh = NULL;

  if (j >= sblock->s_blocks_count)
    {
      ext2_error ("block >= blocks count - block_group = %d, block=%d", i, j);
      j = 0;
      goto sync_out;
    }

  ext2_debug ("allocating block %d; goal hits %d of %d",
	      j, goal_hits, goal_attempts);

  gdp->bg_free_blocks_count--;
  disk_cache_block_ref_ptr (gdp);
  record_global_poke (gdp);

  sblock->s_free_blocks_count--;
  sblock_dirty = 1;

 sync_out:
  assert_backtrace (bh == NULL);
  pthread_spin_unlock (&global_lock);
  alloc_sync (0);

  return j;
}

unsigned long
ext2_count_free_blocks ()
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
      desc_count += gdp->bg_free_blocks_count;
      bh = disk_cache_block_ref (gdp->bg_block_bitmap);
      x = count_free (bh, block_size);
      disk_cache_block_deref (bh);
      printf ("group %d: stored = %d, counted = %lu",
	      i, gdp->bg_free_blocks_count, x);
      bitmap_count += x;
    }
  printf ("ext2_count_free_blocks: stored = %u, computed = %lu, %lu",
	  sblock->s_free_blocks_count, desc_count, bitmap_count);
  pthread_spin_unlock (&global_lock);
  return bitmap_count;
#else
  return sblock->s_free_blocks_count;
#endif
}

static inline int
block_in_use (block_t block, unsigned char *map)
{
  return test_bit ((block - sblock->s_first_data_block) %
		   sblock->s_blocks_per_group, map);
}

void
ext2_check_blocks_bitmap ()
{
  unsigned char *bh;
  unsigned long desc_count, bitmap_count, x;
  unsigned long desc_blocks;
  struct ext2_group_desc *gdp;
  int i, j;

  pthread_spin_lock (&global_lock);

  desc_count = 0;
  bitmap_count = 0;
  gdp = NULL;

  desc_blocks = (groups_count + desc_per_block - 1) / desc_per_block;

  for (i = 0; i < groups_count; i++)
    {
      inline int test_root (int a, int b)
	{
	  if (a == 0)
	    return 1;
	  while (1)
	    {
	      if (a == 1)
		return 1;
	      if (a % b)
		return 0;
	      a = a / b;
	    }
	}
      inline int ext2_group_sparse (int group)
	{
	  return (test_root (group, 3) || test_root (group, 5)
		  || test_root (group, 7));
	}

      gdp = group_desc (i);
      desc_count += gdp->bg_free_blocks_count;
      bh = disk_cache_block_ref (gdp->bg_block_bitmap);

      if (!EXT2_HAS_RO_COMPAT_FEATURE (sblock,
				       EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER)
	  || ext2_group_sparse (i))
	{
	  if (!test_bit (0, bh))
	    ext2_error ("superblock in group %d is marked free", i);

	  for (j = 0; j < desc_blocks; j++)
	    if (!test_bit (j + 1, bh))
	      ext2_error ("descriptor block #%d in group %d is marked free",
			  j, i);
	}

      if (!block_in_use (gdp->bg_block_bitmap, bh))
	ext2_error ("block bitmap for group %d is marked free", i);

      if (!block_in_use (gdp->bg_inode_bitmap, bh))
	ext2_error ("inode bitmap for group %d is marked free", i);

      for (j = 0; j < itb_per_group; j++)
	if (!block_in_use (gdp->bg_inode_table + j, bh))
	  ext2_error ("block #%d of the inode table in group %d is marked free", j, i);

      x = count_free (bh, block_size);
      disk_cache_block_deref (bh);
      if (gdp->bg_free_blocks_count != x)
	ext2_error ("wrong free blocks count for group %d,"
		    " stored = %d, counted = %lu",
		    i, gdp->bg_free_blocks_count, x);
      bitmap_count += x;
    }
  if (sblock->s_free_blocks_count != bitmap_count)
    ext2_error ("wrong free blocks count in super block,"
		" stored = %lu, counted = %lu",
		(unsigned long) sblock->s_free_blocks_count, bitmap_count);
  pthread_spin_unlock (&global_lock);
}
