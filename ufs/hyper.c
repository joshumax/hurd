/* Fetching and storing the hypermetadata (superblock and cg summary info).
   Copyright (C) 1994, 1995 Free Software Foundation

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
#include <string.h>
#include <stdio.h>

static int oldformat = 0;

vm_address_t zeroblock = 0;

struct fs *sblock = 0;
struct csum *csum = 0;

void
get_hypermetadata (void)
{
  if (!sblock)
    sblock = malloc (SBSIZE);

  /* Free previous values.  */
  if (zeroblock)
    vm_deallocate (mach_task_self (), zeroblock, sblock->fs_bsize);
  if (csum)
    free (csum);

  assert (!diskfs_catch_exception ());
  bcopy (disk_image + SBOFF, sblock, SBSIZE);
  diskfs_end_catch_exception ();
  
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
      fprintf (stderr, "Block size %ld is too small (min is %Zd bytes)\n",
	       sblock->fs_bsize, sizeof (struct fs));
      exit (1);
    }

  if (sblock->fs_maxsymlinklen > (long)MAXSYMLINKLEN)
    {
      fprintf (stderr, "Max shortcut symlinklen %ld is too big (max is %ld)\n",
	       sblock->fs_maxsymlinklen, (long)MAXSYMLINKLEN);
      exit (1);
    }

  assert ((__vm_page_size % DEV_BSIZE) == 0);
  assert ((sblock->fs_bsize % DEV_BSIZE) == 0);
  assert (__vm_page_size <= sblock->fs_bsize);

  /* If this is an old filesystem, then we have some more
     work to do; some crucial constants might not be set; we
     are therefore forced to set them here.  */

  if (sblock->fs_npsect < sblock->fs_nsect)
    sblock->fs_npsect = sblock->fs_nsect;

  if (sblock->fs_interleave < 1)
    sblock->fs_interleave = 1;

  if (sblock->fs_postblformat == FS_42POSTBLFMT)
    sblock->fs_nrpos = 8;

  if (sblock->fs_inodefmt < FS_44INODEFMT)
    {
      quad_t sizepb = sblock->fs_bsize;
      int i;

      oldformat = 1;
      sblock->fs_maxfilesize = sblock->fs_bsize * NDADDR - 1;
      for (i = 0; i < NIADDR; i++)
	{
	  sizepb *= NINDIR (sblock);
	  sblock->fs_maxfilesize += sizepb;
	}
      sblock->fs_qbmask = ~sblock->fs_bmask;
      sblock->fs_qfmask = ~sblock->fs_fmask;
    }

  /* Find out if we support the 4.4 symlink/dirtype extension */
  if (sblock->fs_maxsymlinklen > 0)
    direct_symlink_extension = 1;
  else
    direct_symlink_extension = 0;

  csum = malloc (fsaddr (sblock, howmany (sblock->fs_cssize,
					  sblock->fs_fsize)));
  
  assert (!diskfs_catch_exception ());
  bcopy (disk_image + fsaddr (sblock, sblock->fs_csaddr),
	 csum,
	 fsaddr (sblock, howmany (sblock->fs_cssize, sblock->fs_fsize)));
  diskfs_end_catch_exception ();

  if ((diskfs_device_size << diskfs_log2_device_block_size)
      < sblock->fs_size * sblock->fs_fsize)
    {
      fprintf (stderr, 
	       "Disk size (%ld) less than necessary "
	       "(superblock says we need %ld)\n",
	       diskfs_device_size << diskfs_log2_device_block_size,
	       sblock->fs_size * sblock->fs_fsize);
      exit (1);
    }

  vm_allocate (mach_task_self (), &zeroblock, sblock->fs_bsize, 1);

  /* If the filesystem has new features in it, don't pay attention to
     the user's request not to use them. */
  if ((sblock->fs_inodefmt == FS_44INODEFMT
       || direct_symlink_extension)
      && compat_mode == COMPAT_BSD42)
    /* XXX should syslog to this effect */
    compat_mode = COMPAT_BSD44;
}

/* Write the csum data.  This isn't backed by a pager because it is
   taken from ordinary data blocks and might not be an even number
   of pages; in that case writing it through the pager would nuke whatever
   pages came after it on the disk and were backed by file pagers. */
void
diskfs_set_hypermetadata (int wait, int clean)
{
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

  err = diskfs_device_read_sync (fsbtodb (sblock, sblock->fs_csaddr),
				 &buf, bufsize);
  if (!err)
    {  
      bcopy (csum, (void *) buf, sblock->fs_cssize);
      diskfs_device_write_sync (fsbtodb (sblock, sblock->fs_csaddr),
				buf, bufsize);
      csum_dirty = 0;
      vm_deallocate (mach_task_self (), buf, bufsize);
    }
  
  spin_unlock (&alloclock);
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
      sblock->fs_clean = 1;
      sblock_dirty = 1;
    }

  if (sblock_dirty)
    {
      if (sblock->fs_postblformat == FS_42POSTBLFMT
	  || oldformat)
	{
	  char sblockcopy[SBSIZE];
	  struct fs *sbcopy = (struct fs *)sblockcopy;
	  bcopy (sblock, sblockcopy, SBSIZE);
	  if (sblock->fs_postblformat == FS_42POSTBLFMT)
	    sbcopy->fs_nrpos = -1;
	  if (oldformat)
	    {
	      sbcopy->fs_maxfilesize = -1;
	      sbcopy->fs_qbmask = -1;
	      sbcopy->fs_qfmask = -1;
	    }
	  bcopy (sbcopy, disk_image + SBOFF, SBSIZE);
	}
      else
	bcopy (sblock, disk_image + SBOFF, SBSIZE);
      record_poke (disk_image + SBOFF, SBSIZE);
      sblock_dirty = 0;
    }

  if (clean && !diskfs_readonly)
    {
      sblock->fs_clean = 0;
      sblock_dirty = 1;
    }

  spin_unlock (&alloclock);
  diskfs_end_catch_exception ();
}

void diskfs_readonly_changed (int readonly)
{
  vm_protect (mach_task_self (),
	      (vm_address_t)disk_image,
	      diskfs_device_size << diskfs_log2_device_block_size,
	      0, VM_PROT_READ | (readonly ? 0 : VM_PROT_WRITE));

  if (readonly)
    sblock_dirty = 0;
  else
    {
      sblock->fs_clean = 0;
      strcpy (sblock->fs_fsmnt, "Hurd /"); /* XXX */
      sblock_dirty = 1;
      diskfs_set_hypermetadata (1, 0);
    }
}
