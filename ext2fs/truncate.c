/*
 *  linux/fs/ext2/truncate.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/truncate.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Truncate has the most races in the whole filesystem: coding it is
 * a pain in the a**. Especially as I don't do any locking...
 *
 * The code may look a bit weird, but that's just because I've tried to
 * handle things like file-size changes in a somewhat graceful manner.
 * Anyway, truncating a file at the same time somebody else writes to it
 * is likely to result in pretty weird behaviour...
 *
 * The new code handles normal truncates (size = 0) as well as the more
 * general case (size = XXX). I hope.
 */

#define DIRECT_BLOCK(length) \
  ((length + block_size - 1) / block_size)
#define INDIRECT_BLOCK(length, offset) \
  ((int)DIRECT_BLOCK(length) - offset)
#define DINDIRECT_BLOCK(length, offset) \
  (((int)DIRECT_BLOCK(length) - offset) / addr_per_block)
#define TINDIRECT_BLOCK(length) \
  (((int)DIRECT_BLOCK(length) \
    - (addr_per_block * addr_per_block + addr_per_block + EXT2_NDIR_BLOCKS)) \
   / (addr_per_block * addr_per_block))

static int trunc_direct (struct node * node, unsigned long length)
{
  u32 * p;
  int i, tmp;
  char * bh;
  unsigned long block_to_free = 0;
  unsigned long free_count = 0;
  int retry = 0;
  int blocks = block_size / 512;
  int direct_block = DIRECT_BLOCK(length);

 repeat:
  for (i = direct_block ; i < EXT2_NDIR_BLOCKS ; i++)
    {
      p = node->dn.info.i_data + i;
      tmp = *p;
      if (!tmp)
	continue;

      bh = baddr(tmp);

      if (i < direct_block)
	goto repeat;

      if ((bh && bh->b_count != 1) || tmp != *p)
	{
	  retry = 1;
	  continue;
	}

      *p = 0;
      node->dn_stat.st_blocks -= blocks;
      node->dirty = 1;

      if (free_count == 0) {
	block_to_free = tmp;
	free_count++;
      } else if (free_count > 0 && block_to_free == tmp - free_count)
	free_count++;
      else {
	ext2_free_blocks (block_to_free, free_count);
	block_to_free = tmp;
	free_count = 1;
      }
      /*		ext2f_free_blocks (tmp, 1); */
    }

  if (free_count > 0)
    ext2_free_blocks (block_to_free, free_count);

  return retry;
}

static int trunc_indirect (struct node * node, unsigned long length,
			   int offset, u32 * p)
{
  int i, tmp;
  char * bh;
  char * ind_bh;
  u32 * ind;
  unsigned long block_to_free = 0;
  unsigned long free_count = 0;
  int retry = 0;
  int blocks = block_size / 512;
  int indirect_block = INDIRECT_BLOCK (length, offset);

  tmp = *p;
  if (!tmp)
    return 0;
  ind_bh = baddr (tmp);
  if (tmp != *p)
    return 1;
  if (!ind_bh) {
    *p = 0;
    return 0;
  }

 repeat:
  for (i = indirect_block ; i < addr_per_block ; i++)
    {
      if (i < 0)
	i = 0;
      if (i < indirect_block)
	goto repeat;

      ind = i + (u32 *) ind_bh;
      tmp = *ind;
      if (!tmp)
	continue;

      bh = baddr (tmp);
      if (i < indirect_block)
	goto repeat;
      if ((bh && bh->b_count != 1) || tmp != *ind)
	{
	  retry = 1;
	  continue;
	}

      *ind = 0;
      poke_loc (ind, sizeof *ind);

      if (free_count == 0)
	{
	  block_to_free = tmp;
	  free_count++;
	}
      else if (free_count > 0 && block_to_free == tmp - free_count)
	free_count++;
      else
	{
	  ext2_free_blocks (block_to_free, free_count);
	  block_to_free = tmp;
	  free_count = 1;
	}
      /*		ext2_free_blocks (tmp, 1); */
      node->dn_stat.st_blocks -= blocks;
      node->dirty = 1;
    }

  if (free_count > 0)
    ext2_free_blocks (block_to_free, free_count);

  ind = (u32 *) ind_bh;
  for (i = 0; i < addr_per_block; i++)
    if (*(ind++))
      break;
  if (i >= addr_per_block)
    if (ind_bh->b_count != 1)
      retry = 1;
    else
      {
	tmp = *p;
	*p = 0;
	node->dn_stat.st_blocks -= blocks;
	node->dirty = 1;
	ext2_free_blocks (tmp, 1);
      }

  return retry;
}

static int trunc_dindirect (struct node * node, unsigned long length,
			    int offset, u32 * p)
{
  int i, tmp;
  char * dind_bh;
  u32 * dind;
  int retry = 0;
  int blocks = block_size / 512;
  int dindirect_block = DINDIRECT_BLOCK (length, offset);

  tmp = *p;
  if (!tmp)
    return 0;

  dind_bh = baddr (tmp);
  if (tmp != *p)
    return 1;

  if (!dind_bh)
    {
      *p = 0;
      return 0;
    }

 repeat:
  for (i = dindirect_block ; i < addr_per_block ; i++) {
    if (i < 0)
      i = 0;
    if (i < dindirect_block)
      goto repeat;

    dind = i + (u32 *) dind_bh;
    tmp = *dind;
    if (!tmp)
      continue;

    retry |= trunc_indirect (node, offset + (i * addr_per_block), dind);

    poke_loc (dind_bh, block_size);
  }

  dind = (u32 *) dind_bh;
  for (i = 0; i < addr_per_block; i++)
    if (*(dind++))
      break;

  if (i >= addr_per_block)
    if (dind_bh->b_count != 1)
      retry = 1;
    else
      {
	tmp = *p;
	*p = 0;
	node->dn_stat.st_blocks -= blocks;
	node->dirty = 1;
	ext2_free_blocks (tmp, 1);
      }

  return retry;
}

static int trunc_tindirect (struct node * node, unsigned long length)
{
  int i, tmp;
  char * tind_bh;
  u32 * tind, * p;
  int retry = 0;
  int blocks = block_size / 512;
  int tindirect_block = TINDIRECT_BLOCK (length);

  p = node->dn.info.i_data + EXT2_TIND_BLOCK;
  if (!(tmp = *p))
    return 0;

  tind_bh = baddr (tmp);
  if (tmp != *p)
    return 1;

  if (!tind_bh)
    {
      *p = 0;
      return 0;
    }

repeat:
  for (i = tindirect_block ; i < addr_per_block ; i++)
    {
      if (i < 0)
	i = 0;
      if (i < tindirect_block)
	goto repeat;

      tind = i + (u32 *) tind_bh;
      retry |=
	trunc_dindirect(node,
			(EXT2_NDIR_BLOCKS
			 + addr_per_block
			 + (i + 1) * addr_per_block * addr_per_block),
			tind);
      poke_loc (tind_bh, block_size);
    }

  tind = (u32 *) tind_bh;
  for (i = 0; i < addr_per_block; i++)
    if (*(tind++))
      break;

  if (i >= addr_per_block)
    if (tind_bh->b_count != 1)
      retry = 1;
    else
      {
	tmp = *p;
	*p = 0;
	node->dn_stat.st_blocks -= blocks;
	node->dirty = 1;
	ext2_free_blocks (tmp, 1);
      }

  return retry;
}
		
void ext2_truncate (struct node * node)
{
  int retry;
  int offset;
  mode_t mode = node->dn_state.st_mode;

  if (!(S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
    return;
  if (IS_APPEND(node) || IS_IMMUTABLE(node))
    return;

  ext2_discard_prealloc(node);

  while (1)
    {
      down(&node->i_sem);
      retry = trunc_direct(node, length);
      retry |=
	trunc_indirect (node, length, EXT2_IND_BLOCK,
			(u32 *) &node->dn.info.i_data[EXT2_IND_BLOCK]);
      retry |=
	trunc_dindirect (node, length, EXT2_IND_BLOCK +
			 EXT2_ADDR_PER_BLOCK(sblock),
			 (u32 *) &node->dn.info.i_data[EXT2_DIND_BLOCK]);
      retry |= trunc_tindirect (node, length);
      up(&node->i_sem);

      if (!retry)
	break;

      if (IS_SYNC(node) && node->dirty)
	ext2_sync_inode (node);
      current->counter = 0;
      schedule ();
    }

  /*
   * If the file is not being truncated to a block boundary, the
   * contents of the partial block following the end of the file must be
   * zeroed in case it ever becomes accessible again because of
   * subsequent file growth.
   */
  offset = node->allocsize % block_size;
  if (offset)
    {
      char * bh = baddr (node->allocsize / block_size);
      memset (bh + offset, 0, block_size - offset);
      poke_loc (bh + offset, block_size - offset);
    }

  node->dn_set_mtime = 1;
  node->dn_set_ctime = 1;
  node->dirty = 1;
}
