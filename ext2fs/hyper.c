/* Fetching and storing the hypermetadata (superblock and bg summary info)

   Copyright (C) 1994, 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
#include "ext2fs.h"

vm_address_t zeroblock = 0;
char *modified_global_blocks = 0;

static void
allocate_mod_map (void)
{
  static vm_size_t mod_map_size = 0;

  if (modified_global_blocks && mod_map_size)
    /* Get rid of the old one.  */
    vm_deallocate (mach_task_self (),
		   (vm_address_t)modified_global_blocks, mod_map_size);

 if (!diskfs_readonly && block_size < vm_page_size)
    /* If the block size is too small, we have to take extra care when
       writing out pages from the global pager, to make sure we don't stomp
       on any file pager blocks.  In this case use a bitmap to record which
       global blocks are actually modified so the pager can write only them. */
    {
      error_t err;
      /* One bit per filesystem block.  */
      mod_map_size = sblock->s_blocks_count >> 3;
      err =
	vm_allocate (mach_task_self (),
		     (vm_address_t *)&modified_global_blocks, mod_map_size, 1);
      assert_perror (err);
    }
  else
    modified_global_blocks = 0;
}

static int ext2fs_clean;	/* fs clean before we started writing? */

void
get_hypermetadata (void)
{
  error_t err = diskfs_catch_exception ();
  if (err)
    ext2_panic ("can't read superblock: %s", strerror (err));

  if (zeroblock)
    vm_deallocate (mach_task_self (), zeroblock, block_size);

  sblock = (struct ext2_super_block *)boffs_ptr (SBLOCK_OFFS);

  if (sblock->s_magic != EXT2_SUPER_MAGIC
#ifdef EXT2FS_PRE_02B_COMPAT
      && sblock->s_magic != EXT2_PRE_02B_MAGIC
#endif
      )
    ext2_panic ("bad magic number %#x (should be %#x)",
		sblock->s_magic, EXT2_SUPER_MAGIC);

  block_size = EXT2_MIN_BLOCK_SIZE << sblock->s_log_block_size;

  if (block_size > 8192)
    ext2_panic ("block size %ld is too big (max is 8192 bytes)", block_size);

  log2_block_size = ffs (block_size) - 1;
  if ((1 << log2_block_size) != block_size)
    ext2_panic ("block size %ld isn't a power of two!", block_size);

  log2_dev_blocks_per_fs_block
    = log2_block_size - diskfs_log2_device_block_size;
  if (log2_dev_blocks_per_fs_block < 0)
    ext2_panic ("block size %ld isn't a power-of-two multiple of the device"
		" block size (%d)!",
		block_size, diskfs_device_block_size);

  log2_stat_blocks_per_fs_block = 0;
  while ((512 << log2_stat_blocks_per_fs_block) < block_size)
    log2_stat_blocks_per_fs_block++;
  if ((512 << log2_stat_blocks_per_fs_block) != block_size)
    ext2_panic ("block size %ld isn't a power-of-two multiple of 512!",
		block_size);

  if (diskfs_device_size
      < (sblock->s_blocks_count << log2_dev_blocks_per_fs_block))
    ext2_panic ("disk size (%ld blocks) too small "
		"(superblock says we need %ld)",
		diskfs_device_size,
		sblock->s_blocks_count << log2_dev_blocks_per_fs_block);

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

  diskfs_end_catch_exception ();

  /* A handy source of page-aligned zeros.  */
  vm_allocate (mach_task_self (), &zeroblock, block_size, 1);
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
     record_global_poke (sblock);
   }

  sync_global (wait);

  /* Should check writability here and return EROFS if necessary. XXX */
  return 0;
}

void
diskfs_readonly_changed (int readonly)
{
  allocate_mod_map ();

  vm_protect (mach_task_self (),
	      (vm_address_t)disk_image,
	      diskfs_device_size << diskfs_log2_device_block_size,
	      0, VM_PROT_READ | (readonly ? 0 : VM_PROT_WRITE));

  if (!readonly && !(sblock->s_state & EXT2_VALID_FS))
    ext2_warning ("UNCLEANED FILESYSTEM NOW WRITABLE");
}
