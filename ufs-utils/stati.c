/*
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rich $alz of BBN Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static char copyright[] =
"@(#) Copyright (c) 1990, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";

static char copyright[] __attribute__ ((unused));
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)clri.c	8.2 (Berkeley) 9/23/93";
static char sccsid[] __attribute__ ((unused));
#endif /* not lint */

/* Modified by Michael I. Bushnell for GNU Hurd. */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "../ufs/dinode.h"
#include "../ufs/fs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#define MAXPHYS (64 * 1024)
#define DEV_BSIZE 512

/* Returns a nice representation of a file mode in a static buffer.  */
static char *
mode_rep (unsigned short mode)
{
  static char buf[30];
  char *p = buf;

  void add_perms (int shift, unsigned sid_mask)
    {
      unsigned short smode = mode << shift;
      *p++ = (smode & S_IREAD) ? 'r' : '-';
      *p++ = (smode & S_IWRITE) ? 'w' : '-';
      *p++ = (smode & S_IEXEC) ? ((mode & sid_mask) ? 's' : 'x') : '-';
    }

  switch (mode & S_IFMT)
    {
    case S_IFREG: *p++ = '-'; break;
    case S_IFDIR: *p++ = 'd'; break;
    case S_IFCHR: *p++ = 'c'; break;
    case S_IFBLK: *p++ = 'b'; break;
    case S_IFLNK: *p++ = 'l'; break;
    case S_IFSOCK:*p++ = 'p'; break;
    case S_IFIFO: *p++ = 'f'; break;
    default:      *p++ = '?';
    }
  
  add_perms (0, S_ISUID);
  add_perms (3, S_ISGID);
  add_perms (6, 0);

  snprintf (p, buf + sizeof buf - p, " [0%0o]", mode);

  return buf;
}

/* Returns a nice representation of a struct timespec in a static buffer.  */
static char *
timespec_rep (struct timespec *ts)
{
  static char buf[200];
  char *p = buf;
  if (ts->tv_sec || ts->tv_nsec)
    {
      time_t time = ts->tv_sec;
      strcpy (buf, ctime (&time));
      p += strlen (buf);
      if (p[-1] == '\n')
	p--;
      *p++ = ' ';
    }
  snprintf (p, buf + sizeof buf - p, "[%ld, %ld]", ts->tv_sec, ts->tv_nsec);
  return buf;
}

/* Returns a nice representation of a uid in a static buffer.  */
static char *
uid_rep (uid_t uid)
{
  static char buf[200];
  struct passwd *pw = getpwuid (uid);
  if (pw)
    snprintf (buf, sizeof buf, "%d(%s)", uid, pw->pw_name);
  else
    snprintf (buf, sizeof buf, "%d", uid);
  return buf;
}

/* Returns a nice representation of a gid in a static buffer.  */
static char *
gid_rep (gid_t gid)
{
  static char buf[200];
  struct group *gr = getgrgid (gid);
  if (gr)
    snprintf (buf, sizeof buf, "%d(%s)", gid, gr->gr_name);
  else
    snprintf (buf, sizeof buf, "%d", gid);
  return buf;
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	register struct fs *sbp;
	register struct dinode *ip;
	register int fd;
	struct dinode ibuf[MAXBSIZE / sizeof (struct dinode)];
	long bsize;
	off_t offset;
	int inonum;
	char *fs, sblock[SBSIZE];

	if (argc < 3) {
		(void)fprintf(stderr, "usage: stati filesystem inode ...\n");
		exit(1);
	}

	fs = *++argv;

	/* get the superblock. */
	if ((fd = open(fs, O_RDWR, 0)) < 0)
	  {
	    perror (fs);
	    exit (1);
	  }
	if (lseek(fd, (off_t)(SBLOCK * DEV_BSIZE), SEEK_SET) < 0)
	  {
	    perror (fs);
	    exit (1);
	  }
	if (read(fd, sblock, sizeof(sblock)) != sizeof(sblock)) {
		(void)fprintf(stderr,
		    "stati: %s: can't read the superblock.\n", fs);
		exit(1);
	}

	sbp = (struct fs *)sblock;
	if (sbp->fs_magic != FS_MAGIC) {
		(void)fprintf(stderr,
		    "stati: %s: superblock magic number 0x%lux, not 0x%x.\n",
		    fs, sbp->fs_magic, FS_MAGIC);
		exit(1);
	}
	bsize = sbp->fs_bsize;

	/* remaining arguments are inode numbers. */
	while (*++argv) {
		int i;

		/* get the inode number. */
		if ((inonum = atoi(*argv)) <= 0) {
			(void)fprintf(stderr,
			    "stati: %s is not a valid inode number.\n", *argv);
			exit(1);
		}

		/* read in the appropriate block. */
		offset = ino_to_fsba(sbp, inonum);	/* inode to fs blk */
		offset = fsbtodb(sbp, offset);		/* fs blk disk blk */
		offset *= DEV_BSIZE;			/* disk blk to bytes */

		/* seek and read the block */
		if (lseek(fd, offset, SEEK_SET) < 0)
		  {
		    perror (fs);
		    exit (1);
		  }
		if (read(fd, ibuf, bsize) != bsize)
		  {
		    perror (fs);
		    exit (1);
		  }

		/* get the inode within the block. */
		ip = &ibuf[ino_to_fsbo(sbp, inonum)];

		if (argc > 3)
		  printf ("inode:  %d\n", inonum);

		printf ("mode:   %s\n", mode_rep (ip->di_model));
		printf ("nlink:  %d\n", ip->di_nlink);
		printf ("size:   %qd\n", ip->di_size);
		printf ("atime:  %s\n", timespec_rep (&ip->di_atime));
		printf ("mtime:  %s\n", timespec_rep (&ip->di_mtime));
		printf ("ctime:  %s\n", timespec_rep (&ip->di_ctime));
		printf ("flags:  %#lx\n", ip->di_flags);
		printf ("blocks: %ld\n", ip->di_blocks);
		printf ("gener:  %ld\n", ip->di_gen);
		printf ("uid:    %s\n", uid_rep (ip->di_uid));
		printf ("gid:    %s\n", gid_rep (ip->di_gid));
		printf ("dblks:  ");
		for (i = 0; i < NDADDR; i++)
		  printf ("%s%ld", (i == 0 ? "" : ", "), ip->di_db[i]);
		putchar ('\n');
		printf ("iblks:  ");
		for (i = 0; i < NIADDR; i++)
		  printf ("%s%ld", (i == 0 ? "" : ", "), ip->di_ib[i]);
		putchar ('\n');
		printf ("trans:  %ld\n", ip->di_trans);

		if (argv[1])
		  putchar ('\n');
	}
	(void)close(fd);
	exit(0);
}
