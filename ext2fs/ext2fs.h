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
#include "fs.h"

/* Define this if memory objects should not be cached by the kernel.
   Normally, don't define it, but defining it causes a much greater rate
   of paging requests, which may be helpful in catching bugs. */

/* #undef DONT_CACHE_MEMORY_OBJECTS */

struct disknode 
{
  ino_t number;

  /* For a directory, this array holds the number of directory entries in
     each DIRBLKSIZE piece of the directory. */
  int *dirents;

  /* Links on hash list. */
  struct node *hnext, **hprevp;

  struct rwlock allocptrlock;

  struct dirty_indir *dirty;

  struct user_pager_info *fileinfo;
};  

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

/* ---------------------------------------------------------------- */

#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10

extern unsigned long inode_init(unsigned long start, unsigned long end);
extern unsigned long file_table_init(unsigned long start, unsigned long end);
extern unsigned long name_cache_init(unsigned long start, unsigned long end);

#ifndef NULL
#define NULL ((void *) 0)
#endif

/*
 * These are the fs-independent mount-flags: up to 16 flags are supported
 */
#define MS_RDONLY	 1 /* mount read-only */
#define MS_NOSUID	 2 /* ignore suid and sgid bits */
#define MS_NODEV	 4 /* disallow access to device special files */
#define MS_NOEXEC	 8 /* disallow program execution */
#define MS_SYNCHRONOUS	16 /* writes are synced at once */
#define MS_REMOUNT	32 /* alter flags of a mounted FS */

#define S_APPEND    256 /* append-only file */
#define S_IMMUTABLE 512 /* immutable file */

/*
 * Note that read-only etc flags are inode-specific: setting some file-system
 * flags just means all the inodes inherit those flags by default. It might be
 * possible to override it selectively if you really wanted to with some
 * ioctl() that is not currently implemented.
 *
 * Exception: MS_RDONLY is always applied to the entire file system.
 */
#define IS_RDONLY(inode) (((inode)->i_sb) && ((inode)->i_sb->s_flags & MS_RDONLY))
#define IS_APPEND(inode) ((inode)->i_flags & S_APPEND)
#define IS_IMMUTABLE(inode) ((inode)->i_flags & S_IMMUTABLE)

struct super_block
{
  void *s_dev;			/* actually a point to the disk image */
  char *s_devname;
  mach_port_t s_device_port;

  struct user_pager_info *s_pager;
  mach_port_t s_pager_port;
  off_t s_pager_size;

  spin_lock_t file_pagers_lock;

  unsigned long s_blocksize;
  struct mutex s_lock;
  unsigned char s_dirt;
  unsigned long s_flags;
  unsigned long s_magic;
  union {
    struct ext2_sb_info ext2_sb;
  } u;
};

/* ---------------------------------------------------------------- */

spin_lock_t node2pagelock;

spin_lock_t alloclock;

spin_lock_t gennumberlock;
u_long nextgennumber;

/* ---------------------------------------------------------------- */

/* The compat_mode specifies whether or not we write
   extensions onto the disk. */
enum compat_mode
{
  COMPAT_GNU = 0,
  COMPAT_LINUX = 1
} compat_mode;


/* ---------------------------------------------------------------- */

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

#define bread(void *dev, long block, long amount) \
  ((char *)(vm_addres_t)dev + block * DEV_BSIZE)
#define brelse(char *bh) ((void)0)
