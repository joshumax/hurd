/* Common definitions for the ext2 filesystem translator

   Copyright (C) 1995, 1996, 1999, 2002 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
#include <hurd/iohelp.h>
#include <hurd/diskfs.h>
#include <assert.h>
#include <rwlock.h>
#include <sys/mman.h>

#define __hurd__		/* Enable some hurd-specific fields.  */

/* Types used by the ext2 header files.  */
typedef u_int32_t __u32;
typedef int32_t   __s32;
typedef u_int16_t __u16;
typedef int16_t   __s16;
typedef u_int8_t  __u8;
typedef int8_t    __s8;

#include "ext2_fs.h"
#include "ext2_fs_i.h"

#define i_mode_high	osd2.hurd2.h_i_mode_high /* missing from ext2_fs.h */


/* If ext2_fs.h defined a debug routine, undef it and use our own.  */
#undef ext2_debug

#ifdef EXT2FS_DEBUG
extern int ext2_debug_flag;
#define ext2_debug(f, a...) \
 do { if (ext2_debug_flag) printf ("ext2fs: (debug) %s: " f "\n", __FUNCTION__ , ## a); } while (0)
#else
#define ext2_debug(f, a...)	(void)0
#endif

#undef __hurd__

/* Define this if memory objects should not be cached by the kernel.
   Normally, don't define it, but defining it causes a much greater rate
   of paging requests, which may be helpful in catching bugs. */

#undef DONT_CACHE_MEMORY_OBJECTS

int printf (const char *fmt, ...);

/* A block number.  */
typedef __u32 block_t;

/* ---------------------------------------------------------------- */

struct poke
{
  vm_offset_t offset;
  vm_size_t length;
  struct poke *next;
};

struct pokel
{
  struct poke *pokes, *free_pokes;
  spin_lock_t lock;
  struct pager *pager;
  void *image;
};

void pokel_init (struct pokel *pokel, struct pager *pager, void *image);
/* Clean up any state associated with POKEL (but don't free POKEL).  */
void pokel_finalize (struct pokel *pokel);

/* Remember that data here on the disk has been modified. */
void pokel_add (struct pokel *pokel, void *loc, vm_size_t length);

/* Sync all the modified pieces of disk */
void pokel_sync (struct pokel *pokel, int wait);

/* Flush (that is, drop on the ground) all pending pokes in POKEL.  */
void pokel_flush (struct pokel *pokel);

/* Transfer all regions from FROM to POKEL, which must have the same pager. */
void pokel_inherit (struct pokel *pokel, struct pokel *from);

#ifndef EXT2FS_EI
#define EXT2FS_EI extern inline
#endif

/* ---------------------------------------------------------------- */
/* Bitmap routines.  */

#include <stdint.h>

/* Returns TRUE if bit NUM is set in BITMAP.  */
EXT2FS_EI int
test_bit (unsigned num, char *bitmap)
{
  const uint32_t *const bw = (uint32_t *) bitmap + (num >> 5);
  const uint_fast32_t mask = 1 << (num & 31);
  return *bw & mask;
}

/* Sets bit NUM in BITMAP, and returns the previous state of the bit.  Unlike
   the linux version, this function is NOT atomic!  */
EXT2FS_EI int
set_bit (unsigned num, char *bitmap)
{
  uint32_t *const bw = (uint32_t *) bitmap + (num >> 5);
  const uint_fast32_t mask = 1 << (num & 31);
  return (*bw & mask) ?: (*bw |= mask, 0);
}

/* Clears bit NUM in BITMAP, and returns the previous state of the bit.
   Unlike the linux version, this function is NOT atomic!  */
EXT2FS_EI int
clear_bit (unsigned num, char *bitmap)
{
  uint32_t *const bw = (uint32_t *) bitmap + (num >> 5);
  const uint_fast32_t mask = 1 << (num & 31);
  return (*bw & mask) ? (*bw &= ~mask, mask) : 0;
}

/* ---------------------------------------------------------------- */

/* ext2fs specific per-file data.  */
struct disknode
{
  /* For a directory, this array holds the number of directory entries in
     each DIRBLKSIZE piece of the directory. */
  int *dirents;

  /* Links on hash list. */
  struct node *hnext, **hprevp;

  /* Lock to lock while fiddling with this inode's block allocation info.  */
  struct rwlock alloc_lock;

  /* Where changes to our indirect blocks are added.  */
  struct pokel indir_pokel;

  /* Random extra info used by the ext2 routines.  */
  struct ext2_inode_info info;
  uint32_t info_i_translator;	/* That struct from Linux source lacks this. */

  /* This file's pager.  */
  struct pager *pager;

  /* True if the last page of the file has been made writable, but is only
     partially allocated.  */
  int last_page_partially_writable;

  /* Index to start a directory lookup at.  */
  int dir_idx;
};

struct user_pager_info
{
  enum pager_type
    {
      DISK,
      FILE_DATA,
    } type;
  struct node *node;
  vm_prot_t max_prot;
};

/* ---------------------------------------------------------------- */
/* pager.c */

#include <hurd/diskfs-pager.h>

/* Set up the disk pager.  */
void create_disk_pager (void);

/* Call this when we should turn off caching so that unused memory object
   ports get freed.  */
void drop_pager_softrefs (struct node *node);

/* Call this when we should turn on caching because it's no longer
   important for unused memory object ports to get freed.  */
void allow_pager_softrefs (struct node *node);

/* Invalidate any pager data associated with NODE.  */
void flush_node_pager (struct node *node);

/* ---------------------------------------------------------------- */

/* The physical media.  */
extern struct store *store;
/* What the user specified.  */
extern struct store_parsed *store_parsed;

/* Mapped image of the disk.  */
extern void *disk_image;

/* Our in-core copy of the super-block (pointer into the disk_image).  */
struct ext2_super_block *sblock;
/* True if sblock has been modified.  */
int sblock_dirty;

/* Where the super-block is located on disk (at min-block 1).  */
#define SBLOCK_BLOCK	1	/* Default location, second 1k block.  */
#define SBLOCK_SIZE	(sizeof (struct ext2_super_block))
extern unsigned int sblock_block; /* Specified location (in 1k blocks).  */
#define SBLOCK_OFFS	(sblock_block << 10) /* Byte offset of superblock.  */

/* The filesystem block-size.  */
unsigned int block_size;
/* The log base 2 of BLOCK_SIZE.  */
unsigned int log2_block_size;

/* The number of bits to scale min-blocks to get filesystem blocks.  */
#define BLOCKSIZE_SCALE	(sblock->s_log_block_size)

/* log2 of the number of device blocks in a filesystem block.  */
unsigned log2_dev_blocks_per_fs_block;

/* log2 of the number of stat blocks (512 bytes) in a filesystem block.  */
unsigned log2_stat_blocks_per_fs_block;

/* A handy page of page-aligned zeros.  */
vm_address_t zeroblock;

/* Get the superblock from the disk, & setup various global info from it.  */
void get_hypermetadata ();

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

spin_lock_t node_to_page_lock;

spin_lock_t generation_lock;
unsigned long next_generation;

/* ---------------------------------------------------------------- */
/* Functions for looking inside disk_image */

#define trunc_block(offs) (((offs) >> log2_block_size) << log2_block_size)
#define round_block(offs) \
  ((((offs) + block_size - 1) >> log2_block_size) << log2_block_size)

/* block num --> byte offset on disk */
#define boffs(block) ((block) << log2_block_size)
/* byte offset on disk --> block num */
#define boffs_block(offs) ((offs) >> log2_block_size)

/* byte offset on disk --> pointer to in-memory block */
#define boffs_ptr(offs) (((char *)disk_image) + (offs))
/* pointer to in-memory block --> byte offset on disk */
#define bptr_offs(ptr) ((char *)(ptr) - ((char *)disk_image))

/* block num --> pointer to in-memory block */
#define bptr(block) boffs_ptr(boffs(block))
/* pointer to in-memory block --> block num */
#define bptr_block(ptr) boffs_block(bptr_offs(ptr))

/* Get the descriptor for block group NUM.  The block group descriptors are
   stored starting in the filesystem block following the super block.
   We cache a pointer into the disk image for easy lookup.  */
#define group_desc(num)	(&group_desc_image[num])
struct ext2_group_desc *group_desc_image;

#define inode_group_num(inum) (((inum) - 1) / sblock->s_inodes_per_group)

/* Convert an inode number to the dinode on disk. */
EXT2FS_EI struct ext2_inode *
dino (ino_t inum)
{
  unsigned long inodes_per_group = sblock->s_inodes_per_group;
  unsigned long bg_num = (inum - 1) / inodes_per_group;
  unsigned long group_inum = (inum - 1) % inodes_per_group;
  struct ext2_group_desc *bg = group_desc(bg_num);
  block_t block = bg->bg_inode_table + (group_inum / inodes_per_block);
  return ((struct ext2_inode *)bptr(block)) + group_inum % inodes_per_block;
}

/* ---------------------------------------------------------------- */
/* inode.c */

/* Write all active disknodes into the inode pager. */
void write_all_disknodes ();

/* Lookup node INUM (which must have a reference already) and return it
   without allocating any new references. */
struct node *ifind (ino_t inum);

void inode_init (void);

/* ---------------------------------------------------------------- */

/* What to lock if changing global data data (e.g., the superblock or block
   group descriptors or bitmaps).  */
spin_lock_t global_lock;

/* Where to record such changes.  */
struct pokel global_pokel;

/* If the block size is less than the page size, then this bitmap is used to
   record which disk blocks are actually modified, so we don't stomp on parts
   of the disk which are backed by file pagers.  */
char *modified_global_blocks;
spin_lock_t modified_global_blocks_lock;

/* Marks the global block BLOCK as being modified, and returns true if we
   think it may have been clean before (but we may not be sure).  Note that
   this isn't enough to cause the block to be synced; you must call
   record_global_poke to do that.  */
EXT2FS_EI int
global_block_modified (block_t block)
{
  if (modified_global_blocks)
    {
      int was_clean;
      spin_lock (&modified_global_blocks_lock);
      was_clean = !set_bit(block, modified_global_blocks);
      spin_unlock (&modified_global_blocks_lock);
      return was_clean;
    }
  else
    return 1;
}

/* This records a modification to a non-file block.  */
EXT2FS_EI void
record_global_poke (void *ptr)
{
  int boffs = trunc_block (bptr_offs (ptr));
  global_block_modified (boffs_block (boffs));
  pokel_add (&global_pokel, boffs_ptr(boffs), block_size);
}

/* This syncs a modification to a non-file block.  */
EXT2FS_EI void
sync_global_ptr (void *bptr, int wait)
{
  vm_offset_t boffs = trunc_block (bptr_offs (bptr));
  global_block_modified (boffs_block (boffs));
  pager_sync_some (diskfs_disk_pager, trunc_page (boffs), vm_page_size, wait);
}

/* This records a modification to one of a file's indirect blocks.  */
EXT2FS_EI void
record_indir_poke (struct node *node, void *ptr)
{
  int boffs = trunc_block (bptr_offs (ptr));
  global_block_modified (boffs_block (boffs));
  pokel_add (&node->dn->indir_pokel, boffs_ptr(boffs), block_size);
}

/* ---------------------------------------------------------------- */

EXT2FS_EI void
sync_global (int wait)
{
  pokel_sync (&global_pokel, wait);
}

/* Sync all allocation information and node NP if diskfs_synchronous. */
EXT2FS_EI void
alloc_sync (struct node *np)
{
  if (diskfs_synchronous)
    {
      if (np)
	{
	  diskfs_node_update (np, 1);
	  pokel_sync (&np->dn->indir_pokel, 1);
	}
      diskfs_set_hypermetadata (1, 0);
    }
}

/* ---------------------------------------------------------------- */
/* getblk.c */

void ext2_discard_prealloc (struct node *node);

/* Returns in DISK_BLOCK the disk block correspding to BLOCK in NODE.  If
   there is no such block yet, but CREATE is true, then it is created,
   otherwise EINVAL is returned.  */
error_t ext2_getblk (struct node *node, block_t block, int create, block_t *disk_block);

block_t ext2_new_block (block_t goal,
			block_t prealloc_goal,
			block_t *prealloc_count, block_t *prealloc_block);

void ext2_free_blocks (block_t block, unsigned long count);

/* ---------------------------------------------------------------- */

/* Write disk block ADDR with DATA of LEN bytes, waiting for completion.  */
error_t dev_write_sync (block_t addr, vm_address_t data, long len);

/* Write diskblock ADDR with DATA of LEN bytes; don't bother waiting
   for completion. */
error_t dev_write (block_t addr, vm_address_t data, long len);

/* Read disk block ADDR; put the address of the data in DATA; read LEN
   bytes.  Always *DATA should be a full page no matter what.   */
error_t dev_read_sync (block_t addr, vm_address_t *data, long len);

/* ---------------------------------------------------------------- */

#define ext2_error(fmt, args...) _ext2_error (__FUNCTION__, fmt , ##args)
extern void _ext2_error (const char *, const char *, ...)
     __attribute__ ((format (printf, 2, 3)));

#define ext2_panic(fmt, args...) _ext2_panic (__FUNCTION__, fmt , ##args)
extern void _ext2_panic (const char *, const char *, ...)
     __attribute__ ((format (printf, 2, 3)));

extern void ext2_warning (const char *, ...)
     __attribute__ ((format (printf, 1, 2)));
