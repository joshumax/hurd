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
  sblock = malloc (SBLOCK_SIZE);

  assert (!diskfs_catch_exception ());
  bcopy (disk_image + SBLOCK_OFFS, sblock, SBLOCK_SIZE);
  diskfs_end_catch_exception ();
  
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
}

/* Write the csum data.  This isn't backed by a pager because it is
   taken from ordinary data blocks and might not be an even number
   of pages; in that case writing it through the pager would nuke whatever
   pages came after it on the disk and were backed by file pagers. */
void
diskfs_set_hypermetadata (int wait, int clean)
{
#if 0
  vm_address_t buf;
  vm_size_t bufsize;
  error_t err;

  spin_lock (&alloclock);

  if (!csum_dirty)
    {
      spin_unlock (&alloclock);
      return;
    }

  /* Copy into a page-aligned buffer to avoid bugs in kernel device code. */
  
  bufsize = round_page (fragroundup (sblock, sblock->fs_cssize));

  err = dev_read_sync (fsbtodb (sblock, sblock->fs_csaddr), &buf, bufsize);
  if (!err)
    {  
      bcopy (csum, (void *) buf, sblock->fs_cssize);
      dev_write_sync (fsbtodb (sblock, sblock->fs_csaddr), buf, bufsize);
      csum_dirty = 0;
      vm_deallocate (mach_task_self (), buf, bufsize);
    }
  
  spin_unlock (&alloclock);
#endif
}

/* Copy the sblock into the disk */
void
copy_sblock ()
{
  int clean = 1;		/* XXX wrong... */
  
  assert (!diskfs_catch_exception ());

  spin_lock (&sblock_lock);

  if (clean && !diskfs_readonly)
    {
      sblock->s_state |= EXT2_VALID_FS;
      sblock_dirty = 1;
    }

  if (sblock_dirty)
    {
      bcopy (sblock, disk_image + SBLOCK_OFFS, SBLOCK_SIZE);
      pokel_add (&sblock_pokel, disk_image + SBLOCK_OFFS, SBLOCK_SIZE);
      sblock_dirty = 0;
    }

  if (clean && !diskfs_readonly)
    {
      sblock->s_state &= ~EXT2_VALID_FS;
      sblock_dirty = 1;
    }

  spin_unlock (&sblock_lock);
  diskfs_end_catch_exception ();
}
