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

#define SBSIZE (sizeof (struct ext2_super_block))

void
get_hypermetadata (void)
{
  sblock = malloc (SBSIZE);

  disk_sblock = baddr(1);

  assert (!diskfs_catch_exception ());
  bcopy (disk_sblock, sblock, SBSIZE);
  diskfs_end_catch_exception ();
  
  if (sblock->s_magic != EXT2_SUPER_MAGIC
#ifdef EXT2FS_PRE_02B_COMPAT
      && sblock->s_magic != EXT2_PRE_02B_MAGIC
#endif
      )
    ext2_panic("Bad magic number %#lx (should be %#x)",
	       sblock->s_magic, EXT2_SUPER_MAGIC);

  block_size = EXT2_MIN_BLOCK_SIZE << sblock->s_log_block_size;
  if (block_size != BLOCK_SIZE
      && (sb->s_block_size == 1024 || sb->s_block_size == 2048
	  || sb->s_block_size == 4096))
    {
      int offset = (sb_block_num * BLOCK_SIZE) % block_size;
      unsigned long logical_sb_block_num =
	(sb_block_num * BLOCK_SIZE) / block_size;

      disk_sblock = baddr(logical_sb_block_num) + offset;

      assert (!diskfs_catch_exception ());
      bcopy (disk_sblock, sblock, SBSIZE);
      diskfs_end_catch_exception ();

      if (sblock->s_magic != EXT2_SUPER_MAGIC)
	ext2_panic("Bad magic number %#lx (should be %#x)"
		   " in logical superblock!",
		   sblock->s_magic, EXT2_SUPER_MAGIC);
    }

  if (block_size > 8192)
    ext2_panic("Block size %ld is too big (max is 8192 bytes)", block_size);
  if (block_size < SBSIZE)
    ext2_panic ("Block size %ld is too small (min is %ld bytes)",
		block_size, SBSIZE);

  assert ((vm_page_size % DEV_BSIZE) == 0);
  assert ((sblock->fs_bsize % DEV_BSIZE) == 0);
  assert (vm_page_size <= sblock->fs_bsize);
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

  spin_lock (&alloclock);

  if (clean && !diskfs_readonly)
    {
      sblock->s_state |= EXT2_VALID_FS;
      sblock_dirty = 1;
    }

  if (sblock_dirty)
    {
      bcopy (sblock, disk_sblock, SBSIZE);
      record_poke (disk_sblock, SBSIZE);
      sblock_dirty = 0;
    }

  if (clean && !diskfs_readonly)
    {
      sblock->s_state &= ~EXT2_VALID_FS;
      sblock_dirty = 1;
    }

  spin_unlock (&alloclock);
  diskfs_end_catch_exception ();
}
