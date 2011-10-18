/* Miscellaneous functions for fsck
   Copyright (C) 1994,95,96,99,2001,02 Free Software Foundation, Inc.
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
#include <error.h>
#include <time.h>

static void retch (char *reason);

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

struct problem {
  char *desc;
  struct problem *prev;
};

/* A queue of problems found by fsck that are waiting resolution.  The front
   of the list is the most recent problem found (and presumably since
   previous problems haven't been resolved yet, they depend on this one being
   solved for their resolution).  */
static struct problem *problems = 0;

static struct problem *free_problems = 0;

static void
push_problem (char *fmt, va_list args)
{
  struct problem *prob = free_problems;

  if (! prob)
    prob = malloc (sizeof (struct problem));
  else
    problems = prob->prev;
  if (! prob)
    retch ("malloc failed");

  if (vasprintf (&prob->desc, fmt, args) < 0)
    retch ("vasprintf failed");

  prob->prev = problems;
  problems = prob;
}

/* Print the most recent problem, and perhaps how it was resolved.  */
static void
resolve_problem (char *fix)
{
  struct problem *prob = problems;

  if (! prob)
    retch ("no more problems");

  problems = prob->prev;
  prob->prev = free_problems;

  if (preen && device_name)
    printf ("%s: %s", device_name, prob->desc);
  else
    printf ("%s", prob->desc);
  if (fix)
    printf (" (%s)\n", fix);
  else
    putchar ('\n');
  free (prob->desc);
}

/* Retire all problems as if they failed.  We print them in chronological
   order rather than lifo order, as this is a bit clearer, and we can do it
   when we know they're all going to fail.  */
static void
flush_problems ()
{
  struct problem *fail (struct problem *prob)
    {
      struct problem *last = prob->prev ? fail (prob->prev) : prob;
      if (preen && device_name)
	printf ("%s: %s\n", device_name, prob->desc);
      else
	puts (prob->desc);
      free (prob->desc);
      return last;
    }
  if (problems)
    {
      fail (problems)->prev = free_problems;
      free_problems = problems;
    }
}

/* Like printf, but exit after printing. */
void
errexit (char *fmt, ...)
{
  va_list args;

  flush_problems ();

  if (preen && device_name)
    printf ("%s: ", device_name);

  va_start (args, fmt);
  vprintf (fmt, args);
  va_end (args);
  putchar ('\n');

  exit (8);
}

static void
retch (char *reason)
{
  flush_problems ();
  error (99, 0, "(internal error) %s!", reason);
}

/* Prints all unresolved problems and exits, printing MSG as well.  */
static void
punt (char *msg)
{
  problem (0, "%s", msg);
  flush_problems ();
  exit (8);
}

/* If SEVERE is true, and we're in preen mode, then things are too hair to
   fix automatically, so tell the user to do it himself and punt.  */
static void
no_preen (int severe)
{
  if (severe && preen)
    punt ("PLEASE RUN fsck MANUALLY");
}

/* Store away the given message about a problem found.  A call to problem must
   be matched later with a call to pfix, pfail, or reply; to print more
   in the same message, intervening calls to pextend can be used.  If SEVERE is
   true, and we're in preen mode, then the program is terminated.  */
void
problem (int severe, char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  push_problem (fmt, args);
  va_end (args);

  no_preen (severe);
}

/* Following a call to problem (with perhaps intervening calls to
   pmore), appends the given message to that message.  */
void
pextend (char *fmt, ...)
{
  va_list args;
  char *more, *concat;
  struct problem *prob = problems;

  if (! prob)
    retch ("No pending problem to add to");

  va_start (args, fmt);
  if (vasprintf (&more, fmt, args) < 0)
    retch ("vasprintf failed");
  va_end (args);

  concat = realloc (prob->desc, strlen (prob->desc) + 1 + strlen (more) + 1);
  if (! concat)
    retch ("realloc failed");

  strcpy (concat + strlen (concat), more);
  prob->desc = concat;
  free (more);
}

/* Like problem, but as if immediately followed by pfail.  */
void
warning (int severe, char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  push_problem (fmt, args);
  va_end (args);

  no_preen (severe);

  resolve_problem (0);
}

/* Like problem, but appends a helpful description of the given inode number to
   the message.  */
void
pinode (int severe, ino_t ino, char *fmt, ...)
{
  if (fmt)
    {
      va_list args;
      va_start (args, fmt);
      push_problem (fmt, args);
      va_end (args);
    }

  if (ino < ROOTINO || ino > maxino)
    pextend (" (BOGUS INODE) I=%Ld", ino);
  else
    {
      char *p;
      struct dinode dino;
      struct passwd *pw;

      getinode (ino, &dino);

      pextend (" %s I=%Ld", (DI_MODE (&dino) & IFMT) == IFDIR ? "DIR" : "FILE",
	     ino);

      pw = getpwuid (dino.di_uid);
      if (pw)
	pextend (" O=%s", pw->pw_name);
      else
	pextend (" O=%lu", dino.di_uid);

      pextend (" M=0%o", DI_MODE (&dino));
      pextend (" SZ=%llu", dino.di_size);
      p = ctime (&dino.di_mtime.tv_sec);
      pextend (" MT=%12.12s %4.4s", &p[4], &p[20]);
    }

  no_preen (severe);
}

/* Print a successful resolution to a pending problem.  Must follow a call to
   problem or pextend.  */
void
pfix (char *fix)
{
  if (preen)
    resolve_problem (fix ?: "FIXED");
}

/* Print an unsuccessful resolution to a pending problem.  Must follow a call
   to problem or pextend.  */
void
pfail (char *failure)
{
  if (preen)
    resolve_problem (failure);
}

/* Ask the user a question; return 1 if the user says yes, and 0
   if the user says no.  This call must follow a call to problem or pextend,
   which it completes.  */
int
reply (char *question)
{
  int persevere;
  char c;

  if (preen)
    retch ("Got to reply() in preen mode");

  /* Emit the problem to which the question pertains.  */
  resolve_problem (0);

  persevere = !strcmp (question, "CONTINUE");

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
