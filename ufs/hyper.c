/* Fetching and storing the hypermetadata (superblock and cg summary info).
   Copyright (C) 1994 Free Software Foundation

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

#include "ufs.h"
#include "fs.h"
#include <string.h>
#include <stdio.h>

void
get_hypermetadata (void)
{
  error_t err;
  
  err = dev_read_sync (SBLOCK, (vm_address_t *)&sblock, SBSIZE);
  assert (!err);

  if (sblock->fs_magic != FS_MAGIC)
    {
      fprintf (stderr, "Bad magic number %#lx (should be %#x)\n",
	       sblock->fs_magic, FS_MAGIC);
      exit (1);
    }
  if (sblock->fs_bsize > 8192)
    {
      fprintf (stderr, "Block size %ld is too big (max is 8192 bytes)\n",
	       sblock->fs_bsize);
      exit (1);
    }
  if (sblock->fs_bsize < sizeof (struct fs))
    {
      fprintf (stderr, "Block size %ld is too small (min is %ld bytes)\n",
	       sblock->fs_bsize, sizeof (struct fs));
      exit (1);
    }

  /* If this is an old filesystem, then we have some more
     work to do; some crucial constants might not be set; we
     are therefore forced to set them here.  */
  if (sblock->fs_npsect < sblock->fs_nsect)
    sblock->fs_npsect = sblock->fs_nsect;
  if (sblock->fs_interleave < 1)
    sblock->fs_interleave = 1;
  if (sblock->fs_postblformat == FS_42POSTBLFMT)
    sblock->fs_nrpos = 8;

  err = dev_read_sync (fsbtodb (sblock->fs_csaddr), (vm_address_t *) &csum,
		       sblock->fs_fsize * howmany (sblock->fs_cssize, 
						   sblock->fs_fsize));
  assert (!err);
}

/* Write the superblock and cg summary info to disk.  If WAIT is set,
   we must wait for everything to hit the disk; if CLEAN is set, then
   mark the clean bit.  */
void
diskfs_set_hypermetadata (int wait, int clean)
{
  error_t (*writefn) (daddr_t, vm_address_t, long);
  writefn = (wait ? dev_write_sync : dev_write);
  
  spin_lock (&alloclock);
  if (csum_dirty)
    {
      (*writefn)(fsbtodb (sblock->fs_csaddr), (vm_address_t) csum,
		 sblock->fs_fsize * howmany (sblock->fs_cssize,
					     sblock->fs_fsize));
      csum_dirty = 0;
    }

  if (clean)
    {
      sblock->fs_clean = 1;
      sblock_dirty = 1;
    }

  if (sblock_dirty)
    {
      if (sblock->fs_postblformat == FS_42POSTBLFMT)
	{
	  char sblockcopy[SBSIZE];
	  bcopy (sblock, sblockcopy, SBSIZE);
	  ((struct fs *)sblockcopy)->fs_nrpos = -1;
	  (*writefn) (SBLOCK, (vm_address_t) sblockcopy, SBSIZE);
	}
      else
	(*writefn) (SBLOCK, (vm_address_t) sblock, SBSIZE);
      sblock_dirty = 0;
    }

  if (clean)
    {
      sblock->fs_clean = 0;
      sblock_dirty = 1;
    }
  
  spin_unlock (&alloclock);
}


