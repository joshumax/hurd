/*
 *  Mostly taken from: linux/fs/ext2/inode.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Goal-directed block allocation by Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 */

#define inode_bmap(node, nr) ((node)->dn.info.i_data[(nr)])
static inline int 
block_bmap (char *bh, int nr)
{
  return bh ? ((u32 *) bh)[nr] : 0;
}

/* 
 * ext2_discard_prealloc and ext2_alloc_block are atomic wrt. the
 * superblock in the same manner as are ext2_free_blocks and
 * ext2_new_block.  We just wait on the super rather than locking it
 * here, since ext2_new_block will do the necessary locking and we
 * can't block until then.
 */
void 
ext2_discard_prealloc (struct inode *inode)
{
#ifdef EXT2_PREALLOCATE
  if (node->dn->info.i_prealloc_count)
    {
      int i = node->dn->info.i_prealloc_count;
      node->dn->info.i_prealloc_count = 0;
      ext2_free_blocks (node->dn->info.i_prealloc_block, i);
    }
#endif
}

static int 
ext2_alloc_block (struct inode *inode, unsigned long goal)
{
#ifdef EXT2FS_DEBUG
  static unsigned long alloc_hits = 0, alloc_attempts = 0;
#endif
  unsigned long result;
  char *bh;

  wait_on_super (sblock);

#ifdef EXT2_PREALLOCATE
  if (node->dn->info.i_prealloc_count &&
      (goal == node->dn->info.i_prealloc_block ||
       goal + 1 == node->dn->info.i_prealloc_block))
    {
      result = node->dn->info.i_prealloc_block++;
      node->dn->info.i_prealloc_count--;
      ext2_debug ("preallocation hit (%lu/%lu).\n",
		  ++alloc_hits, ++alloc_attempts);

      /* It doesn't matter if we block in getblk() since
         we have already atomically allocated the block, and
         are only clearing it now. */
      if (!(bh = getblk (result, block_size)))
	{
	  ext2_error ("ext2_alloc_block", "cannot get block %lu", result);
	  return 0;
	}
      memset (bh, 0, block_size);
    }
  else
    {
      ext2_discard_prealloc (inode);
      ext2_debug ("preallocation miss (%lu/%lu).\n",
		  alloc_hits, ++alloc_attempts);
      if (S_ISREG (node->dn_stat.mode))
	result = ext2_new_block
	  (goal,
	   &node->dn->info.i_prealloc_count,
	   &node->dn->info.i_prealloc_block);
      else
	result = ext2_new_block (goal, 0, 0);
    }
#else
  result = ext2_new_block (goal, 0, 0);
#endif

  return result;
}


int 
ext2_bmap (struct inode *inode, int block)
{
  int i;
  int addr_per_block = EXT2_ADDR_PER_BLOCK (sblock);

  if (block < 0)
    {
      ext2_warning ("ext2_bmap", "block < 0");
      return 0;
    }
  if (block >= EXT2_NDIR_BLOCKS + addr_per_block +
      addr_per_block * addr_per_block +
      addr_per_block * addr_per_block * addr_per_block)
    {
      ext2_warning ("ext2_bmap", "block > big");
      return 0;
    }
  if (block < EXT2_NDIR_BLOCKS)
    return inode_bmap (inode, block);
  block -= EXT2_NDIR_BLOCKS;
  if (block < addr_per_block)
    {
      i = inode_bmap (inode, EXT2_IND_BLOCK);
      if (!i)
	return 0;
      return block_bmap (baddr (i), block);
    }
  block -= addr_per_block;
  if (block < addr_per_block * addr_per_block)
    {
      i = inode_bmap (inode, EXT2_DIND_BLOCK);
      if (!i)
	return 0;
      i = block_bmap (baddr (i), block / addr_per_block);
      if (!i)
	return 0;
      return block_bmap (baddr (i), block & (addr_per_block - 1));
    }
  block -= addr_per_block * addr_per_block;
  i = inode_bmap (inode, EXT2_TIND_BLOCK);
  if (!i)
    return 0;
  i = block_bmap (baddr (i), block / (addr_per_block * addr_per_block));
  if (!i)
    return 0;
  i = block_bmap (baddr (i), (block / addr_per_block) & (addr_per_block - 1));
  if (!i)
    return 0;
  return block_bmap (baddr (i), block & (addr_per_block - 1));
}

static char *
inode_getblk (struct inode *inode, int nr,
	      int create, int new_block, int *err)
{
  u32 *p;
  int tmp, goal = 0;
  char *result;
  int blocks = block_size / 512;

  p = node->dn->info.i_data + nr;
repeat:
  tmp = *p;
  if (tmp)
    {
      result = getblk (tmp, block_size);
      if (tmp == *p)
	return result;
      goto repeat;
    }
  if (!create || new_block >=
      (current->rlim[RLIMIT_FSIZE].rlim_cur >>
       EXT2_BLOCK_SIZE_BITS (sblock)))
    {
      *err = -EFBIG;
      return NULL;
    }
  if (node->dn->info.i_next_alloc_block == new_block)
    goal = node->dn->info.i_next_alloc_goal;

  ext2_debug ("hint = %d,", goal);

  if (!goal)
    {
      for (tmp = nr - 1; tmp >= 0; tmp--)
	{
	  if (node->dn->info.i_data[tmp])
	    {
	      goal = node->dn->info.i_data[tmp];
	      break;
	    }
	}
      if (!goal)
	goal =
	  (node->dn->info.i_block_group * EXT2_BLOCKS_PER_GROUP (sblock))
	  + sblock->s_first_data_block;
    }

  ext2_debug ("goal = %d.\n", goal);

  tmp = ext2_alloc_block (inode, goal);
  if (!tmp)
    return NULL;
  result = getblk (tmp, block_size);
  if (*p)
    {
      ext2_free_blocks (tmp, 1);
      goto repeat;
    }
  *p = tmp;
  node->dn->info.i_next_alloc_block = new_block;
  node->dn->info.i_next_alloc_goal = tmp;
  node->dn_set_ctime = 1;
  node->dn_stat.st_blocks += blocks;
  if (IS_SYNC (inode) || node->dn->info.i_osync)
    ext2_sync_inode (inode);
  else
    node->dirty = 1;
  return result;
}

static char *
block_getblk (struct inode *inode,
	      char *bh, int nr,
	      int create, int blocksize,
	      int new_block, int *err)
{
  int tmp, goal = 0;
  u32 *p;
  char *result;
  int blocks = block_size / 512;

  if (!bh)
    return NULL;
  if (!bh->b_uptodate)
    {
      ll_rw_block (READ, 1, &bh);
      wait_on_buffer (bh);
      if (!bh->b_uptodate)
	return NULL;
    }
  p = (u32 *) bh + nr;
repeat:
  tmp = *p;
  if (tmp)
    {
      result = getblk (bh->b_dev, tmp, blocksize);
      if (tmp == *p)
	return result;
      goto repeat;
    }
  if (!create || new_block >=
      (current->rlim[RLIMIT_FSIZE].rlim_cur >>
       EXT2_BLOCK_SIZE_BITS (sblock)))
    {
      *err = -EFBIG;
      return NULL;
    }
  if (node->dn->info.i_next_alloc_block == new_block)
    goal = node->dn->info.i_next_alloc_goal;
  if (!goal)
    {
      for (tmp = nr - 1; tmp >= 0; tmp--)
	{
	  if (((u32 *) bh)[tmp])
	    {
	      goal = ((u32 *) bh)[tmp];
	      break;
	    }
	}
      if (!goal)
	goal = bh->b_blocknr;
    }
  tmp = ext2_alloc_block (inode, goal);
  if (!tmp)
    return NULL;

  result = getblk (bh->b_dev, tmp, blocksize);
  if (*p)
    {
      ext2_free_blocks (tmp, 1);
      goto repeat;
    }
  *p = tmp;
  mark_buffer_dirty (bh, 1);
  if (IS_SYNC (inode) || node->dn->info.i_osync)
    {
      ll_rw_block (WRITE, 1, &bh);
      wait_on_buffer (bh);
    }
  node->dn_set_ctime = 1;
  node->dn_stat.st_blocks += blocks;
  node->dirty = 1;
  node->dn->info.i_next_alloc_block = new_block;
  node->dn->info.i_next_alloc_goal = tmp;

  return result;
}

static int 
block_getcluster (struct inode *inode, char *bh,
		  int nr,
		  int blocksize)
{
  u32 *p;
  int firstblock = 0;
  int result = 0;
  int i;

  /* Check to see if clustering possible here. */

  if (!bh)
    return 0;

  if (nr % (PAGE_SIZE / block_size) != 0)
    goto out;
  if (nr + 3 > EXT2_ADDR_PER_BLOCK (sblock))
    goto out;

  for (i = 0; i < (PAGE_SIZE / block_size); i++)
    {
      p = (u32 *) bh + nr + i;

      /* All blocks in cluster must already be allocated */
      if (*p == 0)
	goto out;

      /* See if aligned correctly */
      if (i == 0)
	firstblock = *p;
      else if (*p != firstblock + i)
	goto out;
    }

  p = (u32 *) bh + nr;
  result = generate_cluster (bh->b_dev, (int *) p, blocksize);

out:
  return result;
}

char *
ext2_getblk (struct inode *inode, long block,
	     int create, int *err)
{
  char *bh;
  unsigned long b;
  unsigned long addr_per_block = EXT2_ADDR_PER_BLOCK (sblock);

  *err = -EIO;
  if (block < 0)
    {
      ext2_warning ("ext2_getblk", "block < 0");
      return NULL;
    }
  if (block > EXT2_NDIR_BLOCKS + addr_per_block +
      addr_per_block * addr_per_block +
      addr_per_block * addr_per_block * addr_per_block)
    {
      ext2_warning ("ext2_getblk", "block > big");
      return NULL;
    }
  /*
     * If this is a sequential block allocation, set the next_alloc_block
     * to this block now so that all the indblock and data block
     * allocations use the same goal zone
   */

  ext2_debug ("block %lu, next %lu, goal %lu.\n", block,
	      node->dn->info.i_next_alloc_block,
	      node->dn->info.i_next_alloc_goal);

  if (block == node->dn->info.i_next_alloc_block + 1)
    {
      node->dn->info.i_next_alloc_block++;
      node->dn->info.i_next_alloc_goal++;
    }

  *err = -ENOSPC;
  b = block;
  if (block < EXT2_NDIR_BLOCKS)
    return inode_getblk (inode, block, create, b, err);
  block -= EXT2_NDIR_BLOCKS;
  if (block < addr_per_block)
    {
      bh = inode_getblk (inode, EXT2_IND_BLOCK, create, b, err);
      return block_getblk (inode, bh, block, create,
			   block_size, b, err);
    }
  block -= addr_per_block;
  if (block < addr_per_block * addr_per_block)
    {
      bh = inode_getblk (inode, EXT2_DIND_BLOCK, create, b, err);
      bh = block_getblk (inode, bh, block / addr_per_block, create,
			 block_size, b, err);
      return block_getblk (inode, bh, block & (addr_per_block - 1),
			   create, block_size, b, err);
    }
  block -= addr_per_block * addr_per_block;
  bh = inode_getblk (inode, EXT2_TIND_BLOCK, create, b, err);
  bh = block_getblk (inode, bh, block / (addr_per_block * addr_per_block),
		     create, block_size, b, err);
  bh = block_getblk (inode, bh, (block / addr_per_block) & (addr_per_block - 1),
		     create, block_size, b, err);
  return block_getblk (inode, bh, block & (addr_per_block - 1), create,
		       block_size, b, err);
}

int 
ext2_getcluster (struct inode *inode, long block)
{
  char *bh;
  int err, create;
  unsigned long b;
  unsigned long addr_per_block = EXT2_ADDR_PER_BLOCK (sblock);

  create = 0;
  err = -EIO;
  if (block < 0)
    {
      ext2_warning ("ext2_getblk", "block < 0");
      return 0;
    }
  if (block > EXT2_NDIR_BLOCKS + addr_per_block +
      addr_per_block * addr_per_block +
      addr_per_block * addr_per_block * addr_per_block)
    {
      ext2_warning ("ext2_getblk", "block > big");
      return 0;
    }

  err = -ENOSPC;
  b = block;
  if (block < EXT2_NDIR_BLOCKS)
    return 0;

  block -= EXT2_NDIR_BLOCKS;

  if (block < addr_per_block)
    {
      bh = inode_getblk (inode, EXT2_IND_BLOCK, create, b, &err);
      return block_getcluster (inode, bh, block,
			       block_size);
    }
  block -= addr_per_block;
  if (block < addr_per_block * addr_per_block)
    {
      bh = inode_getblk (inode, EXT2_DIND_BLOCK, create, b, &err);
      bh = block_getblk (inode, bh, block / addr_per_block, create,
			 block_size, b, &err);
      return block_getcluster (inode, bh, block & (addr_per_block - 1),
			       block_size);
    }
  block -= addr_per_block * addr_per_block;
  bh = inode_getblk (inode, EXT2_TIND_BLOCK, create, b, &err);
  bh = block_getblk (inode, bh, block / (addr_per_block * addr_per_block),
		     create, block_size, b, &err);
  bh = block_getblk (inode, bh, (block / addr_per_block) & (addr_per_block - 1),
		     create, block_size, b, &err);
  return block_getcluster (inode, bh, block & (addr_per_block - 1),
			   block_size);
}
