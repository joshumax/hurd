/*
   Copyright (C) 1994, 1996, 1999 Free Software Foundation, Inc.
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

#include "fsck.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <error.h>

static char sblockbuf[SBSIZE];
struct fs *sblock = (struct fs *)sblockbuf;

/* A string identifying what we're trying to check.  */
char *device_name = 0;

daddr_t maxfsblock;
int maxino;
int direct_symlink_extension;

int newinofmt;

int readfd, writefd;

int fix_denied = 0;

int fsmodified = 0;

int lfdir;

/* Get ready to run on device with pathname DEV. */
int
setup (char *dev)
{
  struct stat st;
  int changedsb;
  size_t bmapsize;

  device_name = dev;

  if (stat (dev, &st) == -1)
    {
      error (0, errno, "%s", dev);
      return 0;
    }
  if (!S_ISCHR (st.st_mode) && !S_ISBLK (st.st_mode))
    {
      problem (1, "%s is not a character or block device", dev);
      if (! reply ("CONTINUE"))
	return 0;
    }
  if (preen == 0)
    printf ("** %s", dev);
  if (!nowrite)
    readfd = open (dev, O_RDWR);
  if (nowrite || readfd == -1)
    {
      readfd = open (dev, O_RDONLY);
      if (readfd == -1)
	{
	  error (0, errno, "%s", dev);
	  return 0;
	}
      writefd = -1;
      nowrite = 1;
      if (preen)
	warning (1, "NO WRITE ACCESS");
      printf (" (NO WRITE)");
    }
  else
    writefd = readfd;

  if (preen == 0)
    printf ("\n");

  lfdir = 0;

  /* We don't do the alternate superblock stuff here (yet). */
  readblock (SBLOCK, sblock, SBSIZE);
  changedsb = 0;

  if (sblock->fs_magic != FS_MAGIC)
    {
      warning (1, "BAD MAGIC NUMBER");
      return 0;
    }
  if (sblock->fs_ncg < 1)
    {
      warning (1, "NCG OUT OF RANGE");
      return 0;
    }
  if (sblock->fs_cpg < 1)
    {
      warning (1, "CPG OUT OF RANGE");
      return 0;
    }
  if (sblock->fs_ncg * sblock->fs_cpg < sblock->fs_ncyl
      || (sblock->fs_ncg - 1) * sblock->fs_cpg >= sblock->fs_ncyl)
    {
      warning (1, "NCYL INCONSISTENT WITH NCG AND CPG");
      return 0;
    }
  if (sblock->fs_sbsize > SBSIZE)
    {
      warning (1, "SBLOCK SIZE PREPONTEROUSLY LARGE");
      return 0;
    }
  if (sblock->fs_optim != FS_OPTTIME && sblock->fs_optim != FS_OPTSPACE)
    {
      problem (1, "UNDEFINED OPTIMIZATION IN SUPERBLOCK");
      if (reply ("SET TO DEFAULT"))
	{
	  sblock->fs_optim = FS_OPTTIME;
	  changedsb = 1;
	}
    }
  if (sblock->fs_minfree < 0 || sblock->fs_minfree > 99)
    {
      problem (0, "IMPOSSIBLE MINFREE=%ld IN SUPERBLOCK", sblock->fs_minfree);
      if (preen || reply ("SET TO DEFAULT"))
	{
	  sblock->fs_minfree = 10;
	  changedsb = 1;
	  pfix ("SET TO DEFAULT");
	}
    }
  if (sblock->fs_interleave < 1
      || sblock->fs_interleave > sblock->fs_nsect)
    {
      problem (0, "IMPOSSIBLE INTERLEAVE=%ld IN SUPERBLOCK",
	       sblock->fs_interleave);
      if (preen || reply ("SET TO DEFAULT"))
	{
	  sblock->fs_interleave = 1;
	  changedsb = 1;
	  pfix ("SET TO DEFAULT");
	}
    }
  if (sblock->fs_npsect < sblock->fs_nsect
      || sblock->fs_npsect > sblock->fs_nsect * 2)
    {
      problem (0, "IMPOSSIBLE NPSECT=%ld IN SUPERBLOCK", sblock->fs_npsect);
      if (preen || reply ("SET TO DEFAULT"))
	{
	  sblock->fs_npsect = sblock->fs_nsect;
	  changedsb = 1;
	  pfix ("SET TO DEFAULT");
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

  /* Constants */
  maxfsblock = sblock->fs_size;
  maxino = sblock->fs_ncg * sblock->fs_ipg;
  direct_symlink_extension = sblock->fs_maxsymlinklen > 0;

  /* Allocate and initialize maps */
  bmapsize = roundup (howmany (maxfsblock, NBBY), sizeof (short));
  blockmap = calloc (bmapsize, sizeof (char));
  inodestate = calloc (maxino + 1, sizeof (char));
  typemap = calloc (maxino + 1, sizeof (char));
  linkcount = calloc (maxino + 1, sizeof (nlink_t));
  linkfound = calloc (maxino + 1, sizeof (nlink_t));
  return 1;
}
