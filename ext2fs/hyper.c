/* Fetching and storing the hypermetadata (superblock and bg summary info)

   Copyright (C) 1994,95,96,99,2001,02 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
#include <error.h>
#include <hurd/store.h>
#include "ext2fs.h"

vm_address_t zeroblock;
unsigned char *modified_global_blocks;

static void
allocate_mod_map (void)
{
  static vm_size_t mod_map_size;

  if (modified_global_blocks && mod_map_size)
    /* Get rid of the old one.  */
    munmap (modified_global_blocks, mod_map_size);

 if (!diskfs_readonly && block_size < vm_page_size)
    /* If the block size is too small, we have to take extra care when
       writing out pages from the global pager, to make sure we don't stomp
       on any file pager blocks.  In this case use a bitmap to record which
       global blocks are actually modified so the pager can write only them. */
    {
      /* One bit per filesystem block.  */
      mod_map_size = sblock->s_blocks_count >> 3;
      modified_global_blocks = mmap (0, mod_map_size, PROT_READ|PROT_WRITE,
				     MAP_ANON, 0, 0);
      assert_backtrace (modified_global_blocks != (void *) -1);
    }
  else
    modified_global_blocks = 0;
}

unsigned int sblock_block = SBLOCK_BLOCK; /* in 1k blocks */

static int ext2fs_clean;	/* fs clean before we started writing? */

void
get_hypermetadata (void)
{
  error_t err;
  size_t read = 0;

  if (sblock != NULL)
    munmap (sblock, SBLOCK_SIZE);

  err = store_read (store, SBLOCK_OFFS >> store->log2_block_size,
		    SBLOCK_SIZE, (void **)&sblock, &read);
  if (err || read != SBLOCK_SIZE)
    ext2_panic ("Cannot read hypermetadata");

  if (sblock->s_magic != EXT2_SUPER_MAGIC
#ifdef EXT2FS_PRE_02B_COMPAT
      && sblock->s_magic != EXT2_PRE_02B_MAGIC
#endif
      )
    ext2_panic ("bad magic number %#x (should be %#x)",
		sblock->s_magic, EXT2_SUPER_MAGIC);

  log2_block_size = EXT2_MIN_BLOCK_LOG_SIZE + sblock->s_log_block_size;
  block_size = 1 << log2_block_size;

  if (block_size > EXT2_MAX_BLOCK_SIZE)
    ext2_panic ("block size %d is too big (max is %d bytes)",
		block_size, EXT2_MAX_BLOCK_SIZE);

  log2_dev_blocks_per_fs_block = log2_block_size - store->log2_block_size;
  if (log2_dev_blocks_per_fs_block < 0)
    ext2_panic ("block size %d isn't a power-of-two multiple of the device"
		" block size (%zd)!",
		block_size, store->block_size);

  log2_stat_blocks_per_fs_block = 0;
  while ((512 << log2_stat_blocks_per_fs_block) < block_size)
    log2_stat_blocks_per_fs_block++;
  if ((512 << log2_stat_blocks_per_fs_block) != block_size)
    ext2_panic ("block size %d isn't a power-of-two multiple of 512!",
		block_size);

  if ((store->size >> log2_block_size) < sblock->s_blocks_count)
    ext2_panic ("disk size (%qd bytes) too small; superblock says we need %qd",
		(long long int) store->size,
		(long long int) sblock->s_blocks_count << log2_block_size);
  if (log2_dev_blocks_per_fs_block != 0
      && (store->size & ((1 << log2_dev_blocks_per_fs_block) - 1)) != 0)
    ext2_warning ("%Ld (%zd byte) device blocks "
		  " unused after last filesystem (%d byte) block",
		  (store->size & ((1 << log2_dev_blocks_per_fs_block) - 1)),
		  store->block_size, block_size);

  /* Set these handy variables.  */
  inodes_per_block = block_size / EXT2_INODE_SIZE (sblock);

  frag_size = EXT2_MIN_FRAG_SIZE << sblock->s_log_frag_size;
  if (frag_size)
    frags_per_block = block_size / frag_size;
  else
    ext2_panic ("frag size is zero!");

  if (sblock->s_rev_level > EXT2_GOOD_OLD_REV)
    {
      if (sblock->s_feature_incompat & ~EXT2_FEATURE_INCOMPAT_SUPP)
	ext2_panic ("could not mount because of unsupported optional features"
		    " (0x%x)",
		    sblock->s_feature_incompat & ~EXT2_FEATURE_INCOMPAT_SUPP);
      if (sblock->s_feature_ro_compat & ~EXT2_FEATURE_RO_COMPAT_SUPP)
	{
	  ext2_warning ("mounted readonly because of"
			" unsupported optional features (0x%x)",
			sblock->s_feature_ro_compat & ~EXT2_FEATURE_RO_COMPAT_SUPP);
	  diskfs_readonly = 1;
	}
      if (sblock->s_inode_size != EXT2_GOOD_OLD_INODE_SIZE)
	ext2_panic ("inode size %d isn't supported", sblock->s_inode_size);
    }

  groups_count =
    ((sblock->s_blocks_count - sblock->s_first_data_block +
      sblock->s_blocks_per_group - 1)
     / sblock->s_blocks_per_group);

  itb_per_group = sblock->s_inodes_per_group / inodes_per_block;
  desc_per_block = block_size / sizeof (struct ext2_group_desc);
  addr_per_block = block_size / sizeof (block_t);
  db_per_group = (groups_count + desc_per_block - 1) / desc_per_block;

  ext2fs_clean = sblock->s_state & EXT2_VALID_FS;
  if (! ext2fs_clean)
    {
      ext2_warning ("FILESYSTEM NOT UNMOUNTED CLEANLY; PLEASE fsck");
      if (! diskfs_readonly)
	{
	  diskfs_readonly = 1;
	  ext2_warning ("MOUNTED READ-ONLY; MUST USE `fsysopts --writable'");
	}
    }

  allocate_mod_map ();

  /* A handy source of page-aligned zeros.  */
  if (zeroblock == 0)
    {
      zeroblock = (vm_address_t) mmap (0, block_size, PROT_READ, MAP_ANON, 0, 0);
      assert_backtrace (zeroblock != (vm_address_t) MAP_FAILED);
    }
}

static struct ext2_super_block *mapped_sblock;

void
map_hypermetadata (void)
{
  mapped_sblock = (struct ext2_super_block *) boffs_ptr (SBLOCK_OFFS);

  /* Cache a convenient pointer to the block group descriptors for allocation.
     These are stored in the filesystem blocks following the superblock.  */
  group_desc_image =
    (struct ext2_group_desc *) bptr (bptr_block (mapped_sblock) + 1);
}

error_t
diskfs_set_hypermetadata (int wait, int clean)
{
  if (clean && ext2fs_clean && !(sblock->s_state & EXT2_VALID_FS))
    /* The filesystem is clean, so we need to set the clean flag.  */
    {
      sblock->s_state |= EXT2_VALID_FS;
      sblock_dirty = 1;
    }
  else if (!clean && (sblock->s_state & EXT2_VALID_FS))
    /* The filesystem just became dirty, so clear the clean flag.  */
    {
      sblock->s_state &= ~EXT2_VALID_FS;
      sblock_dirty = 1;
      wait = 1;
    }

 if (sblock_dirty)
   {
     sblock_dirty = 0;
     memcpy (mapped_sblock, sblock, SBLOCK_SIZE);
     disk_cache_block_ref_ptr (mapped_sblock);
     record_global_poke (mapped_sblock);
   }

  sync_global (wait);

  /* Should check writability here and return EROFS if necessary. XXX */
  return 0;
}

void
diskfs_readonly_changed (int readonly)
{
  allocate_mod_map ();

  (*(readonly ? store_set_flags : store_clear_flags)) (store, STORE_READONLY);

  mprotect (disk_cache, disk_cache_size,
	    PROT_READ | (readonly ? 0 : PROT_WRITE));

  if (!readonly && !(sblock->s_state & EXT2_VALID_FS))
    ext2_warning ("UNCLEANED FILESYSTEM NOW WRITABLE");
}
