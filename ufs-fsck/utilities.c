/* Miscellaneous functions for fsck
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

#include "fsck.h"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

/* Read disk block ADDR into BUF of SIZE bytes. */
void
readblock (daddr_t addr, void *buf, size_t size)
{
  if (lseek (readfd, addr * DEV_BSIZE, L_SET) == -1)
    errexit ("CANNOT SEEK TO BLOCK %ld", addr);
  if (read (readfd, buf, size) != size)
    errexit ("CANNOT READ BLOCK %ld", addr);
}

/* Write disk block BLKNO from BUF of SIZE bytes. */
void
writeblock (daddr_t addr, void *buf, size_t size)
{
  if (lseek (writefd, addr * DEV_BSIZE, L_SET) == -1)
    errexit ("CANNOT SEEK TO BLOCK %ld", addr);
  if (write (writefd, buf, size) != size)
    errexit ("CANNOT READ BLOCK %ld", addr);
  fsmodified = 1;
}

/* Read inode number INO into DINODE. */
void
getinode (ino_t ino, struct dinode *di)
{
  daddr_t iblk;
  char buf[sblock->fs_fsize];

  iblk = ino_to_fsba (sblock, ino);
  readblock (fsbtodb (sblock, iblk), buf, sblock->fs_fsize);
  bcopy (buf + ino_to_fsbo (sblock, ino), di, sizeof (struct dinode));
}

/* Write inode number INO from DINODE. */
void
write_inode (ino_t ino, struct dinode *di)
{
  daddr_t iblk;
  char buf[sblock->fs_fsize];
  
  iblk = ino_to_fsba (sblock, ino);
  readblock (fsbtodb (sblock, iblk), buf, sblock->fs_fsize);
  bcopy (di, buf + ino_to_fsbo (sblock, ino), sizeof (struct dinode));
  writeblock (fsbtodb (sblock, iblk), buf, sblock->fs_fsize);
}

/* Clear inode number INO and zero DI. */
void
clear_inode (ino_t ino, struct dinode *di)
{
  bzero (di, sizeof (struct dinode));
  write_inode (ino, di);
}

/* Allocate and return a block and account for it in all the block
   maps locally.  Don't trust or change the disk block maps.
   The block should be NFRAGS fragments long.  */
daddr_t
allocblk (int nfrags)
{
  daddr_t i;
  int j, k;
  
  if (nfrags <= 0 || nfrags > sblock->fs_frag)
    return 0;
  
  /* Examine each block of the filesystem.  */
  for (i = 0; i < maxfsblock - sblock->fs_frag; i += sblock->fs_frag)
    {
      /* For each piece of the block big enough to hold this frag... */
      for (j = 0; j <= sblock->fs_frag - nfrags; j++)
	{
	  /* For each frag of this piece... */
	  for (k = 0; k < nfrags; k++)
	    if (testbmap (i + j + k))
	      break;
	  
	  /* If one of the frags was allocated... */
	  if (k < nfrags)
	    {
	      /* Skip at least that far (short cut) */
	      j += k;
	      continue;
	    }
	  
	  /* It's free (at address i + j) */

	  /* Mark the frags allocated in our map */
	  for (k = 0; k < nfrags; k++)
	    setbmap (i + j + k);
	  
	  return (i + j);
	}
    }
  return 0;
}

/* Check if a block starting at BLK and extending for CNT 
   fragments is out of range; if it is, then return 1; otherwise return 0. */
int
check_range (daddr_t blk, int cnt)
{
  int c;
  
  if ((unsigned)(blk + cnt) > maxfsblock)
    return 1;
  
  c = dtog (sblock, blk);
  if (blk < cgdmin (sblock, c))
    {
      if (blk + cnt > cgsblock (sblock, c))
	return 1;
    }
  else
    {
      if (blk + cnt > cgbase (sblock, c + 1))
	return 1;
    }
  
  return 0;
}  

