/* 
   Copyright (C) 1994 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */


/* Get ready to run on device with pathname DEV. */
setup (char *dev)
{
  struct stat st;
  int changedsb;
  
  if (stat (dev, &st) == -1)
    {
      perror (dev);
      return 0;
    }
  if (!S_ISDIR (st.st_mode))
    {
      pfatal ("%s is not a character device", dev);
      if (!reply ("CONTINUE"))
	return 0;
    }
  readfd = open (dev, O_RDONLY);
  if (readfd == -1)
    {
      perror (dev);
      return 0;
    }
  if (preen == 0)
    printf ("** %s", dev);
  if (nflag)
    writefd = -1;
  else
    writefd = open (dev, O_WRONLY);
  if (nflag || writefd == -1)
    {
      if (preen)
	pfatal ("NO WRITE ACCESS");
      printf (" (NO WRITE)");
    }
  if (preen == 0)
    printf ("\n");
  
  lfdir = 0;

  /* We don't do the alternate superblock stuff here (yet). */
  readblock (SBLOCK, sblock, SBSIZE);
  changedsb = 0;

  if (sblock->fs_magic != FS_MAGIC)
    {
      pfatal ("BAD MAGIC NUMBER");
      return 0;
    }
  if (sblock->fs_ncg < 1)
    {
      pfatal ("NCG OUT OF RANGE");
      return 0;
    }
  if (sblock->fs_cpg < 1)
    {
      pfatal ("CPG OUT OF RANGE");
      return 0;
    }
  if (sblock->fs_ncg * sblock->fs_cpg > sblock->fs_ncyl)
    {
      pfatal ("NCYL LESS THAN NCG*CPG");
      return 0;
    }
  if (sblock->fs_sbsize > SBSIZE)
    {
      pfatal ("SBLOCK SIZE PREPONTEROUSLY LARGE");
      return 0;
    }
  if (sblock->fs_optim != FS_OPTTIME && sblock->fs_optim != FS_OPTSPACE)
    {
      pfatal ("UNDEFINED OPTIMIZATION IN SUPERBLOCK");
      if (reply ("SET TO DEFAULT"))
	{
	  sblock->fs_optim = FS_OPTTIME;
	  changedsb = 1;
	}
    }
  if (sblock->fs_minfree < 0 || sblock->fs_minfree > 99)
    {
      pfatal ("IMPOSSIBLE MINFREE=%d IN SUPERBLOCK", sblock->fs_minfree);
      if (reply ("SET TO DEFAULT"))
	{
	  sblock->fs_minfree = 10;
	  changedsb = 1;
	}
    }
  if (sblock->fs_interleave < 1
      || sblock->fs_interleave > sblock->fs_nsect)
    {
      pwarn ("IMPOSSIBLE INTERLEAVE=%d IN SUPERBLOCK", sblock->fs_interleave);
      if (preen || reply ("SET TO DEFAULT"))
	{
	  if (preen)
	    printf (" (SET TO DEFAULT)");
	  sblock->fs_interleave = 1;
	  changedsb = 1;
	}
    }
  if (sblock->fs_npsect < sblock->fs_nsect
      || sblock->npsect > sblock->fs_nsect * 2)
    {
      pwarn ("IMPOSSIBLE NPSECT=%d IN SUPERBLOCK", sblock->fs_npsect);
      if (preen || reply ("SET TO DEFAULT"))
	{
	  if (preen)
	    printf (" (SET TO DEFAULT)");
	  sblock->fs_npsect = sblock->fs_nsect;
	  changedsb = 1;
	}
    }
  if (sblock->fs_inodefmt >= FS_44INODEFMT)
    newinofmt = 1;
  else
    {
      sblock->fs_qbmask = ~sblock->fs_bmask;
      sblock->fs_qfmask = ~sblock->fs_fmask;
      newinofmt = 0;
    }
  
  if (changedsb)
    writeblock (SBLOCK, sblock, SBSIZE);
      
  /* Allocate and initialize maps */
  bmapsize = roundup (howmany (maxfsblock, NBBY), sizeof (short));
  blockmap = calloc (bmapsize, sizeof (char));
  inodestate = calloc (maxino + 1, sizeof (char));
  typemap = calloc (maxino + 1, sizeof (char));
  linkcount = calloc (maxino + 1, sizeof (nlink_t));
  linkfound = calloc (maxino + 1, sizeof (nlink_t));
  return 1;
}

      
	
