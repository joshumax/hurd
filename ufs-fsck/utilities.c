/* Miscellaneous functions for fsck
   Copyright (C) 1994, 1995, 1996 Free Software Foundation, Inc.
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
#include <stdarg.h>
#include <pwd.h>

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
    errexit ("CANNOT WRITE BLOCK %ld", addr);
  fsmodified = 1;
}

/* Last filesystem fragment that we read an inode from */
static char *lastifrag;
static daddr_t lastifragaddr;

/* Read inode number INO into DINODE. */
void
getinode (ino_t ino, struct dinode *di)
{
  daddr_t iblk;

  if (!lastifrag)
    lastifrag = malloc (sblock->fs_bsize);
  
  iblk = ino_to_fsba (sblock, ino);
  if (iblk != lastifragaddr)
    readblock (fsbtodb (sblock, iblk), lastifrag, sblock->fs_bsize);
  lastifragaddr = iblk;
  bcopy (lastifrag + ino_to_fsbo (sblock, ino) * sizeof (struct dinode), 
	 di, sizeof (struct dinode));
}

/* Write inode number INO from DINODE. */
void
write_inode (ino_t ino, struct dinode *di)
{
  daddr_t iblk;
  
  iblk = ino_to_fsba (sblock, ino);
  if (iblk != lastifragaddr)
    readblock (fsbtodb (sblock, iblk), lastifrag, sblock->fs_bsize);
  lastifragaddr = iblk;
  bcopy (di, lastifrag + ino_to_fsbo (sblock, ino) * sizeof (struct dinode),
	 sizeof (struct dinode));
  writeblock (fsbtodb (sblock, iblk), lastifrag, sblock->fs_bsize);
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

/* Like printf, but exit if we are preening. */
int
pfatal (char *fmt, ...)
{
  va_list args;
  int ret;

  if (preen && device_name)
    printf ("%s: ", device_name);
  
  va_start (args, fmt);
  ret = vprintf (fmt, args);
  va_end (args);
  putchar ('\n');
  if (preen)
    exit (1);
  
  return ret;
}

/* Like printf, but exit after printing. */
void
errexit (char *fmt, ...)
{
  va_list args;

  if (preen && device_name)
    printf ("%s: ", device_name);

  va_start (args, fmt);
  vprintf (fmt, args);
  va_end (args);
  putchar ('\n');
  exit (1);
}

/* Like printf, but give more information (when we fully support it) 
   when preening. */
int
pwarn (char *fmt, ...)
{
  va_list args;
  int ret;

  if (preen && device_name)
    printf ("%s: ", device_name);

  va_start (args, fmt);
  ret = vprintf (fmt, args);
  va_end (args);

  return ret;
}

/* Print how a problem was fixed in preen mode.  */
void
pfix (char *fix)
{
  if (preen)
    printf (" (%s)\n", fix);
}

/* Ask the user a question; return 1 if the user says yes, and 0
   if the user says no. */
int
reply (char *question)
{
  int persevere;
  char c;
  
  if (preen)
    pfatal ("INTERNAL ERROR: GOT TO reply ()");

  persevere = !strcmp (question, "CONTINUE");
  putchar ('\n');
  if (!persevere && (nowrite || writefd < 0))
    {
      fix_denied = 1;
      printf ("%s? no\n\n", question);
      return 0;
    }
  else if (noquery || (persevere && nowrite))
    {
      printf ("%s? yes\n\n", question);
      return 1;
    }
 else
    {
      do
	{
	  printf ("%s? [yn] ", question);
	  fflush (stdout);
	  c = getchar ();
	  while (c != '\n' && getchar () != '\n')
	    if (feof (stdin))
	      {
		fix_denied = 1;
		return 0;
	      }
	}
      while (c != 'y' && c != 'Y' && c != 'n' && c != 'N');
      putchar ('\n');
      if (c == 'y' || c == 'Y')
	return 1;
      else
	{
	  fix_denied = 1;
	  return 0;
	} 
    }
}

/* Print a helpful description of the given inode number. */
void
pinode (ino_t ino, char *fmt, ...)
{
  if (fmt)
    {
      va_list args;
      va_start (args, fmt);
      vprintf (fmt, args);
      va_end (args);
      putchar (' ');
    }

  if (ino < ROOTINO || ino > maxino)
    printf (" NODE I=%d", ino);
  else
    {
      char *p;
      struct dinode dino;
      struct passwd *pw;

      getinode (ino, &dino);

      printf ("%s I=%d", (DI_MODE (&dino) & IFMT) == IFDIR ? "DIR" : "FILE",
	      ino);

      pw = getpwuid (dino.di_uid);
      if (pw)
	printf (" OWNER=%s", pw->pw_name);
      else
	printf (" OWNER=%lu", dino.di_uid);
  
      printf (" MODE=%o", DI_MODE (&dino));
      printf (" SIZE=%llu ", dino.di_size);
      p = ctime (&dino.di_mtime.ts_sec);
      printf (" MTIME=%12.12s %4.4s", &p[4], &p[20]);
    }
}

