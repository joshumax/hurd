/* 
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

#include <sys/types.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include "../ufs/fs.h"
#include "../ufs/dinode.h"
#include "../ufs/dir.h"

/* Type of an inode */
#define UNALLOC 0
#define REG 1
#define DIRECTORY 2
#define BADDIR 3

/* Added to directories in pass 2 */
#define DIR_REF 4	/* dir has been found in connectivity search */

/* State of each inode (set by pass 1) */
char *inodestate;

/* Number of links claimed by each inode (set by pass 1) */
nlink_t *linkcount;

/* Number of links found to each inode (set by pass 2) */
nlink_t *linkfound;

/* DT_foo type of each inode (set by pass 1) */
char *typemap;

/* Map of blocks allocated */
char *blockmap;

/* A string identifying what we're trying to check.  */
extern char *device_name;


/* Command line flags */ 
int nowrite;			/* all questions fail */
int noquery;			/* all questions succeed */


enum contret
{
  RET_STOP,
  RET_GOOD,
  RET_BAD,
};


/* One of these structures is set up for each directory by
   pass 1 and used by passes 2 and 3. */
struct dirinfo
{
  struct inoinfo *i_nexthash;	/* next entry in hash chain */
  ino_t i_number;		/* inode entry of this dir */
  ino_t i_parent;		/* inode entry of parent */
  ino_t i_dotdot;		/* inode number of `..' */
  ino_t i_dot;			/* inode number of `.' */
  ino_t i_isize;		/* size of inode */
  u_int i_numblks;		/* size of block array in bytes */
  daddr_t i_blks[0];		/* array of inode block addresses */
};

/* Array of all the dirinfo structures in inode number order */
struct dirinfo **dirarray;

/* Array of all thi dirinfo structures sorted by their first
   block address */
struct dirinfo **dirsorted;

int dirarrayused;		/* number of directories */
int dirarraysize;		/* alloced size of dirarray/dirsorted */

struct dups {
	struct dups *next;
	daddr_t dup;
};
struct dups *duplist;		/* head of dup list */
struct dups *muldup;		/* end of unique duplicate dup block numbers */


struct fs *sblock;

daddr_t maxfsblock;
int maxino;
int direct_symlink_extension;

int newinofmt;

int preen;

int readfd, writefd;

int fix_denied;

int fsmodified;

int lfdir;

mode_t lfmode;
extern char *lfname;

#define NBBY 8
#define howmany(x,y) (((x)+((y)-1))/(y))
#define roundup(x, y) ((((x)+((y)-1))/(y))*(y))
#define isclr(a, i) (((a)[(i)/NBBY] & (1<<((i)%NBBY))) == 0)
#define isset(a, i) ((a)[(i)/NBBY] & (1<<((i)%NBBY)))
#define setbit(a,i) ((a)[(i)/NBBY] |= 1<<((i)%NBBY))
#define clrbit(a,i) ((a)[(i)/NBBY] &= ~(1<<(i)%NBBY))
#define DEV_BSIZE 512

#define setbmap(blkno) setbit (blockmap, blkno)
#define testbmap(blkno) isset (blockmap, blkno)
#define clrbmap(blkno) clrbit (blockmap, blkno)

#define DI_MODE(dp) (((dp)->di_modeh << 16) | (dp)->di_model)


     
int setup (char *);
void pass1 (), pass1b (), pass2 (), pass3 (), pass4 (), pass5 ();

void readblock (daddr_t, void *, size_t);
void writeblock (daddr_t, void *, size_t);

void getinode (ino_t, struct dinode *);
void write_inode (ino_t, struct dinode *);
void clear_inode (ino_t, struct dinode *);

daddr_t allocblk (int);
int check_range (daddr_t, int);

ino_t allocino (ino_t, mode_t);
void freeino (ino_t);
ino_t allocdir (ino_t, ino_t, mode_t);

int makeentry (ino_t, ino_t, char *);
int changeino (ino_t, char *, ino_t);

int linkup (ino_t, ino_t);

void datablocks_iterate (struct dinode *, int (*)(daddr_t, int, off_t));
void allblock_iterate (struct dinode *, int (*)(daddr_t, int, off_t));

void record_directory (struct dinode *, ino_t);
struct dirinfo *lookup_directory (ino_t);

int reply (char *);
void pfix (char *fix);
void pinode (ino_t, char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
int pwarn (char *, ...) __attribute__ ((format (printf, 1, 2)));
int pfatal (char *, ...)  __attribute__ ((format (printf, 1, 2)));
void errexit (char *, ...) __attribute__ ((format (printf, 1, 2),
					   noreturn));
