/* Fetching and storing the hypermetadata (superblock and cg summary info).
   Copyright (C) 1994, 95, 96, 97, 98, 1999 Free Software Foundation, Inc.

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
#include <error.h>
#include <hurd/store.h>

static int ufs_clean;		/* fs clean before we started writing? */

static int oldformat;

void *zeroblock;

struct fs *sblock;
struct csum *csum;

void
swab_sblock (struct fs *sblock)
{
  int i, j;

  sblock->fs_sblkno = swab_long (sblock->fs_sblkno);
  sblock->fs_cblkno = swab_long (sblock->fs_cblkno);
  sblock->fs_iblkno = swab_long (sblock->fs_iblkno);
  sblock->fs_dblkno = swab_long (sblock->fs_dblkno);
  sblock->fs_cgoffset = swab_long (sblock->fs_cgoffset);
  sblock->fs_cgmask = swab_long (sblock->fs_cgmask);
  sblock->fs_time = swab_long (sblock->fs_time);
  sblock->fs_size = swab_long (sblock->fs_size);
  sblock->fs_dsize = swab_long (sblock->fs_dsize);
  sblock->fs_ncg = swab_long (sblock->fs_ncg);
  sblock->fs_bsize = swab_long (sblock->fs_bsize);
  sblock->fs_fsize = swab_long (sblock->fs_fsize);
  sblock->fs_frag = swab_long (sblock->fs_frag);
  sblock->fs_minfree = swab_long (sblock->fs_minfree);
  sblock->fs_rotdelay = swab_long (sblock->fs_rotdelay);
  sblock->fs_rps = swab_long (sblock->fs_rps);
  sblock->fs_bmask = swab_long (sblock->fs_bmask);
  sblock->fs_fmask = swab_long (sblock->fs_fmask);
  sblock->fs_bshift = swab_long (sblock->fs_bshift);
  sblock->fs_fshift = swab_long (sblock->fs_fshift);
  sblock->fs_maxcontig = swab_long (sblock->fs_maxcontig);
  sblock->fs_maxbpg = swab_long (sblock->fs_maxbpg);
  sblock->fs_fragshift = swab_long (sblock->fs_fragshift);
  sblock->fs_fsbtodb = swab_long (sblock->fs_fsbtodb);
  sblock->fs_sbsize = swab_long (sblock->fs_sbsize);
  sblock->fs_csmask = swab_long (sblock->fs_csmask);
  sblock->fs_csshift = swab_long (sblock->fs_csshift);
  sblock->fs_nindir = swab_long (sblock->fs_nindir);
  sblock->fs_inopb = swab_long (sblock->fs_inopb);
  sblock->fs_nspf = swab_long (sblock->fs_nspf);
  sblock->fs_optim = swab_long (sblock->fs_optim);
  sblock->fs_npsect = swab_long (sblock->fs_npsect);
  sblock->fs_interleave = swab_long (sblock->fs_interleave);
  sblock->fs_trackskew = swab_long (sblock->fs_trackskew);
  sblock->fs_headswitch = swab_long (sblock->fs_headswitch);
  sblock->fs_trkseek = swab_long (sblock->fs_trkseek);
  sblock->fs_csaddr = swab_long (sblock->fs_csaddr);
  sblock->fs_cssize = swab_long (sblock->fs_cssize);
  sblock->fs_cgsize = swab_long (sblock->fs_cgsize);
  sblock->fs_ntrak = swab_long (sblock->fs_ntrak);
  sblock->fs_nsect = swab_long (sblock->fs_nsect);
  sblock->fs_spc = swab_long (sblock->fs_spc);
  sblock->fs_ncyl = swab_long (sblock->fs_ncyl);
  sblock->fs_cpg = swab_long (sblock->fs_cpg);
  sblock->fs_ipg = swab_long (sblock->fs_ipg);
  sblock->fs_fpg = swab_long (sblock->fs_fpg);
  sblock->fs_cstotal.cs_ndir = swab_long (sblock->fs_cstotal.cs_ndir);
  sblock->fs_cstotal.cs_nbfree = swab_long (sblock->fs_cstotal.cs_nbfree);
  sblock->fs_cstotal.cs_nifree = swab_long (sblock->fs_cstotal.cs_nifree);
  sblock->fs_cstotal.cs_nffree = swab_long (sblock->fs_cstotal.cs_nffree);
  /* fs_fmod, fs_clean, fs_ronly, fs_flags, fs_fsmnt are all char */
  sblock->fs_cgrotor = swab_long (sblock->fs_cgrotor);
  sblock->fs_cpc = swab_long (sblock->fs_cpc);
  sblock->fs_contigsumsize = swab_long (sblock->fs_contigsumsize);
  sblock->fs_maxsymlinklen = swab_long (sblock->fs_maxsymlinklen);
  sblock->fs_inodefmt = swab_long (sblock->fs_inodefmt);
  sblock->fs_maxfilesize = swab_long_long (sblock->fs_maxfilesize);
  sblock->fs_qbmask = swab_long_long (sblock->fs_qbmask);
  sblock->fs_state = swab_long (sblock->fs_state);
  sblock->fs_postblformat = swab_long (sblock->fs_postblformat);
  sblock->fs_nrpos = swab_long (sblock->fs_nrpos);
  sblock->fs_postbloff = swab_long (sblock->fs_postbloff);
  sblock->fs_rotbloff = swab_long (sblock->fs_rotbloff);
  sblock->fs_magic = swab_long (sblock->fs_magic);

  /* Tables */
  if (sblock->fs_postblformat == FS_42POSTBLFMT)
    for (i = 0; i < 16; i++)
      for (j = 0; j < 8; j++)
	sblock->fs_opostbl[i][j] = swab_short (sblock->fs_opostbl[i][j]);
  else
    for (i = 0; i < sblock->fs_cpc; i++)
      for (j = 0; j < sblock->fs_nrpos; j++)
	fs_postbl(sblock, j)[i]
	  = swab_short (fs_postbl (sblock, j)[i]);

  /* The rot table is all chars */
}

void
swab_csums (struct csum *csum)
{
  int i;

  for (i = 0; i < sblock->fs_ncg; i++)
    {
      csum[i].cs_ndir = swab_long (csum[i].cs_ndir);
      csum[i].cs_nbfree = swab_long (csum[i].cs_nbfree);
      csum[i].cs_nifree = swab_long (csum[i].cs_nifree);
      csum[i].cs_nffree = swab_long (csum[i].cs_nffree);
    }
}

void
get_hypermetadata (void)
{
  error_t err;

  if (!sblock)
    sblock = malloc (SBSIZE);

  /* Free previous values.  */
  if (zeroblock)
    munmap ((caddr_t) zeroblock, sblock->fs_bsize);
  if (csum)
    free (csum);

  err = diskfs_catch_exception ();
  assert_perror (err);
  bcopy (disk_image + SBOFF, sblock, SBSIZE);
  diskfs_end_catch_exception ();

  if ((swab_long (sblock->fs_magic)) == FS_MAGIC)
    {
      swab_disk = 1;
      swab_sblock (sblock);
    }
  else
    swab_disk = 0;

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

  /* Examine the clean bit and force read-only if unclean.  */
  ufs_clean = sblock->fs_clean;
  if (! ufs_clean)
    {
      error (0, 0,
	     "%s: warning: FILESYSTEM NOT UNMOUNTED CLEANLY; PLEASE fsck",
	     diskfs_disk_name);
      if (! diskfs_readonly)
	{
	  diskfs_readonly = 1;
	  error (0, 0,
		 "%s: MOUNTED READ-ONLY; MUST USE `fsysopts --writable'",
		 diskfs_disk_name);
	}
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

  if (swab_disk)
    swab_csums (csum);

  if (store->size < sblock->fs_size * sblock->fs_fsize)
    {
      fprintf (stderr,
	       "Disk size (%Ld) less than necessary "
	       "(superblock says we need %ld)\n",
	       store->size, sblock->fs_size * sblock->fs_fsize);
      exit (1);
    }

  zeroblock = mmap (0, sblock->fs_bsize, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);

  /* If the filesystem has new features in it, don't pay attention to
     the user's request not to use them. */
  if ((sblock->fs_inodefmt == FS_44INODEFMT
       || direct_symlink_extension)
      && compat_mode == COMPAT_BSD42)
    {
      compat_mode = COMPAT_BSD44;
      error (0, 0,
	     "4.2 compat mode requested on 4.4 fs--switched to 4.4 mode");
    }
}

/* Write the csum data.  This isn't backed by a pager because it is
   taken from ordinary data blocks and might not be an even number
   of pages; in that case writing it through the pager would nuke whatever
   pages came after it on the disk and were backed by file pagers. */
error_t
diskfs_set_hypermetadata (int wait, int clean)
{
  error_t err;

  pthread_spin_lock (&alloclock);

  if (csum_dirty)
    {
      /* Copy into a page-aligned buffer to avoid bugs in kernel device
         code. */
      void *buf = 0;
      size_t read = 0;
      size_t bufsize = round_page (fragroundup (sblock, sblock->fs_cssize));

      err = store_read (store,
			fsbtodb (sblock, sblock->fs_csaddr)
			  << log2_dev_blocks_per_dev_bsize,
			bufsize, &buf, &read);
      if (err)
	return err;
      else if (read != bufsize)
	err = EIO;
      else
	{
	  size_t wrote;
	  bcopy (csum, buf, sblock->fs_cssize);
	  if (swab_disk)
	    swab_csums ((struct csum *)buf);
	  err = store_write (store,
			     fsbtodb (sblock, sblock->fs_csaddr)
			       << log2_dev_blocks_per_dev_bsize,
			     buf, bufsize, &wrote);
	  if (!err && wrote != bufsize)
	    err = EIO;
	}

      munmap (buf, read);

      if (err)
	{
	  pthread_spin_unlock (&alloclock);
	  return err;
	}

      csum_dirty = 0;
    }

  if (clean && ufs_clean && !sblock->fs_clean)
    {
      /* The filesystem is clean, so set the clean flag.  */
      sblock->fs_clean = 1;
      sblock_dirty = 1;
    }
  else if (!clean && sblock->fs_clean)
    {
      /* Clear the clean flag */
      sblock->fs_clean = 0;
      sblock_dirty = 1;
      wait = 1;			/* must be synchronous */
    }

  pthread_spin_unlock (&alloclock);

  /* Update the superblock if necessary (clean bit was just set).  */
  copy_sblock ();

  sync_disk (wait);
  return 0;
}

/* Copy the sblock into the disk */
void
copy_sblock ()
{
  error_t err;

  err = diskfs_catch_exception ();
  assert_perror (err);

  pthread_spin_lock (&alloclock);

  if (sblock_dirty)
    {
      assert (! diskfs_readonly);

      if (sblock->fs_postblformat == FS_42POSTBLFMT
	  || oldformat
	  || swab_disk)
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
	  if (swab_disk)
	    swab_sblock (sbcopy);
	  bcopy (sbcopy, disk_image + SBOFF, SBSIZE);
	}
      else
	bcopy (sblock, disk_image + SBOFF, SBSIZE);
      record_poke (disk_image + SBOFF, SBSIZE);
      sblock_dirty = 0;
    }

  pthread_spin_unlock (&alloclock);

  diskfs_end_catch_exception ();
}

void
diskfs_readonly_changed (int readonly)
{
  (*(readonly ? store_set_flags : store_clear_flags)) (store, STORE_READONLY);

  mprotect (disk_image, store->size, PROT_READ | (readonly ? 0 : PROT_WRITE));

  if (readonly)
    {
      /* We know we are sync'd now.  The superblock is marked as dirty
	 because we cleared the clean flag immediately after sync'ing.
	 But now we want to leave it marked clean and not touch it further.  */
      sblock_dirty = 0;
      return;
    }

  strcpy (sblock->fs_fsmnt, "Hurd /"); /* XXX */

  if (!sblock->fs_clean)
    error (0, 0, "WARNING: UNCLEANED FILESYSTEM NOW WRITABLE");
}
