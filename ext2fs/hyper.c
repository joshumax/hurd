/* Fetching and storing the hypermetadata (superblock and bg summary info).

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

#include <string.h>
#include <stdio.h>
#include "ext2fs.h"

void
get_hypermetadata (void)
{
  assert (!diskfs_catch_exception ());

  sblock = boffs_ptr(SBLOCK_OFFS);
  
  if (sblock->s_magic != EXT2_SUPER_MAGIC
#ifdef EXT2FS_PRE_02B_COMPAT
      && sblock->s_magic != EXT2_PRE_02B_MAGIC
#endif
      )
    ext2_panic("get_hypermetadata",
	       "Bad magic number %#x (should be %#x)",
	       sblock->s_magic, EXT2_SUPER_MAGIC);

  block_size = EXT2_MIN_BLOCK_SIZE << sblock->s_log_block_size;

  if (block_size > 8192)
    ext2_panic("get_hypermetadata",
	       "Block size %ld is too big (max is 8192 bytes)", block_size);
  if (block_size < SBLOCK_SIZE)
    ext2_panic ("get_hypermetadata",
		"Block size %ld is too small (min is %ld bytes)",
		block_size, SBLOCK_SIZE);

  log2_dev_blocks_per_fs_block = 0;
  while ((device_block_size << log2_dev_blocks_per_fs_block) < block_size)
    log2_dev_blocks_per_fs_block++;
  if ((device_block_size << log2_dev_blocks_per_fs_block) != block_size)
    ext2_panic("get_hypermetadata",
	       "Block size %ld isn't a power-of-two multiple of the device"
	       " block size (%d)!",
	       block_size, device_block_size);

  log2_block_size = 0;
  while ((1 << log2_block_size) < block_size)
    log2_block_size++;
  if ((1 << log2_block_size) != block_size)
    ext2_panic("get_hypermetadata",
	       "Block size %ld isn't a power of two!", block_size);

  /* Set these handy variables.  */
  inodes_per_block = block_size / sizeof (struct ext2_inode);

  frag_size = EXT2_MIN_FRAG_SIZE << sblock->s_log_frag_size;
  if (frag_size)
    frags_per_block = block_size / frag_size;
  else
    ext2_panic("get_hypermetadata", "Frag size is zero!");

  groups_count =
    ((sblock->s_blocks_count - sblock->s_first_data_block +
      sblock->s_blocks_per_group - 1)
     / sblock->s_blocks_per_group);

  itb_per_group = sblock->s_inodes_per_group / inodes_per_block;
  desc_per_block = block_size / sizeof (struct ext2_group_desc);
  addr_per_block = block_size / sizeof (u32);
  db_per_group = (groups_count + desc_per_block - 1) / desc_per_block;

  diskfs_end_catch_exception ();
}

void
diskfs_set_hypermetadata (int wait, int clean)
{
  if (clean && !(sblock->s_state & EXT2_VALID_FS))
    /* The filesystem is clean, so we need to set the clean flag.  We
       just write the superblock directly, without using the paged copy.  */
    {
      struct ext2_super_block clean_sblock;

      bcopy (sblock, clean_sblock, SBLOCK_SIZE);
      clean_sblock->s_state |= EXT2_VALID_FS;
      dev_write_sync (SBLOCK_OFFS / device_block_size,
		      (vm_address_t)clean_sblock, SBLOCK_SIZE);
    }
}
