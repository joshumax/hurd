/* 
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

#include <mach.h>
#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/pager.h>
#include <hurd/fshelp.h>
#include <hurd/ioserver.h>
#include <hurd/diskfs.h>
#include <assert.h>
#include <rwlock.h>
#include "ext2_fs.h"

/* Define this if memory objects should not be cached by the kernel.
   Normally, don't define it, but defining it causes a much greater rate
   of paging requests, which may be helpful in catching bugs. */

/* #undef DONT_CACHE_MEMORY_OBJECTS */

/* Identifies a particular block and where it's found
   when interpreting indirect block structure.  */
struct iblock_spec
{
  /* Disk address of block */
  daddr_t bno;

  /* Offset in next block up; -1 if it's in the inode itself. */
  int offset;
};

/* Identifies an indirect block owned by this file which
   might be dirty. */
struct dirty_indir
{
  daddr_t bno;			/* Disk address of block. */
  struct dirty_indir *next;
};

struct user_pager_info 
{
  struct node *np;
  enum pager_type 
    {
      DISK,
      FILE_DATA,
    } type;
  struct pager *p;
  struct user_pager_info *next, **prevp;
};

struct user_pager_info *diskpager;
mach_port_t diskpagerport;
off_t diskpagersize;

vm_address_t zeroblock;

struct fs *sblock;
struct csum *csum;
int sblock_dirty;
int csum_dirty;

void *disk_image;

spin_lock_t node2pagelock;

spin_lock_t alloclock;

spin_lock_t gennumberlock;
u_long nextgennumber;

mach_port_t ufs_device;

/* The compat_mode specifies whether or not we write
   extensions onto the disk. */
enum compat_mode
{
  COMPAT_GNU = 0,
  COMPAT_LINUX = 1
} compat_mode;


/* Handy macros */
#define DEV_BSIZE 512
#define NBBY 8
#define btodb(n) ((n) / DEV_BSIZE)
#define howmany(x,y) (((x)+((y)-1))/(y))
#define roundup(x, y) ((((x)+((y)-1))/(y))*(y))
#define isclr(a, i) (((a)[(i)/NBBY] & (1<<((i)%NBBY))) == 0)
#define isset(a, i) ((a)[(i)/NBBY] & (1<<((i)%NBBY)))
#define setbit(a,i) ((a)[(i)/NBBY] |= 1<<((i)%NBBY))
#define clrbit(a,i) ((a)[(i)/NBBY] &= ~(1<<(i)%NBBY))
#define fsaddr(fs,n) (fsbtodb(fs,n)*DEV_BSIZE)


/* Functions for looking inside disk_image */

/* Sync part of the disk */
extern inline void
sync_disk_blocks (daddr_t blkno, size_t nbytes, int wait)
{
  pager_sync_some (diskpager->p, fsaddr (sblock, blkno), nbytes, wait);
}

/* Sync an disk inode */
extern inline void
sync_dinode (int inum, int wait)
{
  sync_disk_blocks (ino_to_fsba (sblock, inum), sblock->fs_fsize, wait);
}
