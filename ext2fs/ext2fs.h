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

  struct ext2_inode_info info;

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

/* ---------------------------------------------------------------- */

struct user_pager_info *diskpager;
mach_port_t diskpagerport;
off_t diskpagersize;

void *disk_image;
char *devname;
mach_port_t ext2fs_device;

/* Our in-core copy of the super-block.  */
struct ext2_super_block *sblock;
/* What to lock if changing the super block.  */
spin_lock_t sblock_lock;
/* Where the super-block is on the disk.  */
char *disk_sblock;
/* True if sblock has been modified.  */
int sblock_dirty;

/* The filesystem block-size.  */
unsigned long block_size;

/* ---------------------------------------------------------------- */
/* Random stuff calculated from the super block.  */

unsigned long frag_size;	/* Size of a fragment in bytes */
unsigned long frags_per_block;	/* Number of fragments per block */
unsigned long inodes_per_block;	/* Number of inodes per block */

unsigned long itb_per_group;	/* Number of inode table blocks per group */
unsigned long db_per_group;	/* Number of descriptor blocks per group */
unsigned long desc_per_block;	/* Number of group descriptors per block */
unsigned long addr_per_block;	/* Number of disk addresses per block */

unsigned long groups_count;	/* Number of groups in the fs */

/* ---------------------------------------------------------------- */
spin_lock_t node2pagelock;

spin_lock_t alloclock;

spin_lock_t gennumberlock;
u_long nextgennumber;

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

/* Functions for looking inside disk_image */

/* Returns a pointer to the disk block BLOCK.  */
#define baddr(block) (((char *)disk_image) + (block) * DEV_BSIZE)

/* Get the descriptor for the block group inode INUM is in.  */
extern inline struct ext2_group_desc *
group_desc(unsigned long bg_num)
{
  int desc_per_block = EXT2_DESC_PER_BLOCK(sblock);
  unsigned long group_desc = bg_num / desc_per_block;
  unsigned long desc = bg_num % desc_per_block;
  return ((struct ext2_group_desc *)baddr(sb_block_num + group_desc)) + desc;
}

#define inode_group_num(inum) (((inum) - 1) / sblock->s_inodes_per_group)

/* Convert an inode number to the dinode on disk. */
extern inline struct ext2_inode *
dino (ino_t inum)
{
  unsigned long bg_num = inode_group_num(inum);
  struct ext2_group_desc *bg = group_desc(bg_num);
  unsigned long inodes_per_block = EXT2_INODES_PER_BLOCK(sblock);
  unsigned long block = bg.bg_inode_table + (bg_num / inodes_per_block);
  return ((struct ext2_inode *)baddr(block)) + inum % inodes_per_block;
}

/* Convert a indirect block number to a daddr_t table. */
extern inline daddr_t *
indir_block (daddr_t bno)
{
  return (daddr_t *) (disk_image + fsaddr (sblock, bno));
}

/* Convert a cg number to the cylinder group. */
extern inline struct cg *
cg_locate (int ncg)
{
  return (struct cg *) (disk_image + fsaddr (sblock, cgtod (sblock, ncg)));
}

/* Sync part of the disk */
extern inline void
sync_disk_image (char *place, size_t nbytes, int wait)
{
  pager_sync_some (diskpager->p, place - disk_image, nbytes, wait);
}

/* Sync an disk inode */
extern inline void
sync_dinode (int inum, int wait)
{
  sync_disk_blocks (dino (inum), sizeof (struct ext2_inode), wait);
}

/* ---------------------------------------------------------------- */

/* Sync all allocation information and node NP if diskfs_synchronous. */
inline void
alloc_sync (struct node *np)
{
  if (diskfs_synchronous)
    {
      if (np)
	diskfs_node_update (np, 1);
      copy_sblock ();
      diskfs_set_hypermetadata (1, 0);
      sync_disk (1);
    }
}
