/* 
   Copyright (C) 1994, 1995 Free Software Foundation, Inc.

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

#define __hurd__		/* Enable some hurd-specific fields.  */
#include "ext2_fs.h"
#include "ext2_fs_i.h"
#undef __hurd__

/* Define this if memory objects should not be cached by the kernel.
   Normally, don't define it, but defining it causes a much greater rate
   of paging requests, which may be helpful in catching bugs. */

/* #undef DONT_CACHE_MEMORY_OBJECTS */

int printf (const char *fmt, ...);

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

/* Remember that data here on the disk has been modified. */
void pokel_add (struct pokel *pokel, void *loc, vm_size_t length);

/* Sync all the modified pieces of disk */
void pokel_sync (struct pokel *pokel, int wait);

/* ---------------------------------------------------------------- */
/* Bitmap routines.  */

/* Returns TRUE if bit NUM is set in BITMAP.  */
extern inline int
test_bit (unsigned num, char *bitmap)
{
  return bitmap[num >> 3] & (1 << (num & 0x7));
}

/* Sets bit NUM in BITMAP, and returns the previous state of the bit.  */
extern inline int
set_bit (unsigned num, char *bitmap)
{
  char *p = bitmap + (num >> 3);
  char byte = *p;
  char mask = (1 << (num & 0x7));

  if (byte & mask)
    return 1;
  else
    {
      *p = byte | mask;
      return 0;
    }
}

/* Clears bit NUM in BITMAP, and returns the previous state of the bit.  */
extern inline int
clear_bit (unsigned num, char *bitmap)
{
  char *p = bitmap + (num >> 3);
  char byte = *p;
  char mask = (1 << (num & 0x7));

  if (byte & mask)
    {
      *p = byte & ~mask;
      return 1;
    }
  else
    return 0;
}

/* Counts the number of bits unset in MAP, a bitmap NUMCHARS long. */
unsigned long count_free (char * map, unsigned int numchars);

extern int find_first_zero_bit(void * addr, unsigned size);

extern int find_next_zero_bit (void * addr, int size, int offset);

extern unsigned long ffz(unsigned long word);

/* Returns a pointer to the first occurence of CH in the buffer BUF of len
   LEN, or BUF + LEN if CH doesn't occur.  */
void *memscan(void *buf, unsigned char ch, unsigned len);

/* ---------------------------------------------------------------- */

struct disknode 
{
  ino_t number;

  /* For a directory, this array holds the number of directory entries in
     each DIRBLKSIZE piece of the directory. */
  int *dirents;

  /* Links on hash list. */
  struct node *hnext, **hprevp;

  struct rwlock alloc_lock;

  struct pokel pokel;

  struct ext2_inode_info info;

  struct user_pager_info *fileinfo;
};  

struct user_pager_info 
{
  struct node *node;
  enum pager_type 
    {
      DISK,
      FILE_DATA,
    } type;
  struct pager *p;
  struct user_pager_info *next, **prevp;
};

/* ---------------------------------------------------------------- */
/* pager.c */

struct user_pager_info *disk_pager;
mach_port_t disk_pager_port;
void *disk_image;

/* Create the global disk pager.  */
void create_disk_pager ();

/* Call this when we should turn off caching so that unused memory object
   ports get freed.  */
void drop_pager_softrefs (struct node *node);

/* Call this when we should turn on caching because it's no longer
   important for unused memory object ports to get freed.  */
void allow_pager_softrefs (struct node *node);

/* ---------------------------------------------------------------- */

/*
 * These are the fs-independent mount-flags: up to 16 flags are supported
 */
#define MS_RDONLY	 1 /* mount read-only */
#define MS_NOSUID	 2 /* ignore suid and sgid bits */
#define MS_NODEV	 4 /* disallow access to device special files */
#define MS_NOEXEC	 8 /* disallow program execution */
#define MS_SYNCHRONOUS	16 /* writes are synced at once */
#define MS_REMOUNT	32 /* alter flags of a mounted FS */

/* Inode flags.  */
#define S_APPEND    256 /* append-only file */
#define S_IMMUTABLE 512 /* immutable file */

#define IS_APPEND(node) ((node)->dn->info.i_flags & S_APPEND)
#define IS_IMMUTABLE(node) ((node)->dn->info.i_flags & S_IMMUTABLE)

/* ---------------------------------------------------------------- */

char *device_name;
mach_port_t device_port;
off_t device_size;
unsigned device_block_size;

/* Our in-core copy of the super-block.  */
struct ext2_super_block *sblock;
/* True if sblock has been modified.  */
int sblock_dirty;

/* Where the super-block is located on disk (at min-block 1).  */
#define SBLOCK_BLOCK 1
#define SBLOCK_OFFS (SBLOCK_BLOCK * EXT2_MIN_BLOCK_SIZE)
#define SBLOCK_SIZE (sizeof (struct ext2_super_block))

/* The filesystem block-size.  */
unsigned long block_size;
/* The log base 2 of BLOCK_SIZE.  */
unsigned log2_block_size;

/* log2 of the number of device blocks (DEVICE_BLOCK_SIZE) in a filesystem
   block (BLOCK_SIZE).  */
unsigned log2_dev_blocks_per_fs_block;

/* log2 of the number of stat blocks (512 bytes) in a filesystem block.  */
unsigned log2_stat_blocks_per_fs_block;

/* A handy page of page-aligned zeros.  */
vm_address_t zeroblock;

/* Get the superblock from the disk, & setup various global info from it.  */
error_t get_hypermetadata ();

/* ---------------------------------------------------------------- */

/* Returns a single page page-aligned buffer.  */
vm_address_t get_page_buf ();

/* Frees a block returned by get_page_buf.  */
void free_page_buf (vm_address_t buf);

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
   stored starting in the block following the super block.  */
extern inline struct ext2_group_desc *
group_desc(unsigned long num)
{
  int desc_per_block = EXT2_DESC_PER_BLOCK(sblock);
  unsigned long group_desc = num / desc_per_block;
  unsigned long desc = num % desc_per_block;
  return
    ((struct ext2_group_desc *)boffs_ptr(SBLOCK_OFFS + boffs(1 + group_desc)))
      + desc;
}

#define inode_group_num(inum) (((inum) - 1) / sblock->s_inodes_per_group)

/* Convert an inode number to the dinode on disk. */
extern inline struct ext2_inode *
dino (ino_t inum)
{
  unsigned long inodes_per_group = sblock->s_inodes_per_group;
  unsigned long bg_num = (inum - 1) / inodes_per_group;
  unsigned long group_inum = (inum - 1) % inodes_per_group;
  struct ext2_group_desc *bg = group_desc(bg_num);
  unsigned long inodes_per_block = EXT2_INODES_PER_BLOCK(sblock);
  unsigned long block = bg->bg_inode_table + (group_inum / inodes_per_block);
  return ((struct ext2_inode *)bptr(block)) + group_inum % inodes_per_block;
}

/* ---------------------------------------------------------------- */
/* inode.c */

/* Write all active disknodes into the inode pager. */
void write_all_disknodes ();

/* Fetch inode INUM, set *NPP to the node structure; gain one user reference
   and lock the node.  */
error_t iget (ino_t inum, struct node **npp);

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
extern inline int
global_block_modified (daddr_t block)
{
  if (modified_global_blocks)
    {
      int was_clean;
      spin_lock (&modified_global_blocks_lock);
      was_clean = !set_bit(block, modified_global_blocks);
      spin_unlock (&modified_global_blocks_lock);
 if (was_clean)
   printf ("Marked block %lu as modified\n", block);
      return was_clean;
    }
  else
    return 1;
}

/* This records a modification to a non-file block.  */
extern inline void
record_global_poke (void *ptr)
{
  int boffs = trunc_block (bptr_offs (ptr));
  if (global_block_modified (boffs_block (boffs)))
 {
 printf ("Adding block %u to global_pokel (%p)\n", boffs_block (boffs), &global_pokel);
    pokel_add (&global_pokel, boffs_ptr(boffs), block_size);
  }
}

/* This syncs a modification to a non-file block.  */
extern inline void
sync_global_ptr (void *bptr, int wait)
{
  vm_offset_t boffs = trunc_block (bptr_offs (bptr));
  global_block_modified (boffs_block (boffs));
 printf ("Syncing block %d\n", boffs_block (boffs));
  pager_sync_some (disk_pager->p, trunc_page (boffs), vm_page_size, wait);
}

/* This records a modification to one of a file's indirect blocks.  */
extern inline void
record_indir_poke (struct node *node, void *ptr)
{
  int boffs = trunc_block (bptr_offs (ptr));
  if (global_block_modified (boffs_block (boffs)))
 {
 printf ("Adding block %u to indir pokel for inode %u (%p)\n", boffs_block
	 (boffs), node->dn->number, &node->dn->pokel);
    pokel_add (&node->dn->pokel, boffs_ptr(boffs), block_size);
  }
}

/* ---------------------------------------------------------------- */

extern inline void
sync_super_block ()
{
 printf ("Syncing superblock\n");
  sblock_dirty = 0;		/* It doesn't matter if this gets stomped.  */
  sync_global_ptr (sblock, 1);
}

extern inline void
sync_global_data ()
{
 printf ("Syncing global data\n");
  pokel_sync (&global_pokel, 1);
  diskfs_set_hypermetadata (1, 0);
}

/* Sync all allocation information and node NP if diskfs_synchronous. */
extern inline void
alloc_sync (struct node *np)
{
  if (diskfs_synchronous)
    {
      if (np)
	{
 printf ("Alloc sync inode %d\n", np->dn->number);
	  diskfs_node_update (np, 1);
	  pokel_sync (&np->dn->pokel, 1);
	}
else  printf ("Alloc sync 0\n");
      sync_global_data ();
    }
}

/* ---------------------------------------------------------------- */
/* getblk.c */

void ext2_discard_prealloc (struct node *node);

error_t ext2_getblk (struct node *node, long block, int create, char **buf);

int ext2_new_block (unsigned long goal, u32 * prealloc_count, u32 * prealloc_block);

void ext2_free_blocks (unsigned long block, unsigned long count);

/* ---------------------------------------------------------------- */

/* Write disk block ADDR with DATA of LEN bytes, waiting for completion.  */
error_t dev_write_sync (daddr_t addr, vm_address_t data, long len);

/* Write diskblock ADDR with DATA of LEN bytes; don't bother waiting
   for completion. */
error_t dev_write (daddr_t addr, vm_address_t data, long len);

/* Read disk block ADDR; put the address of the data in DATA; read LEN
   bytes.  Always *DATA should be a full page no matter what.   */
error_t dev_read_sync (daddr_t addr, vm_address_t *data, long len);

/* ---------------------------------------------------------------- */

extern void ext2_error (const char *, const char *, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void ext2_panic (const char *, const char *, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void ext2_warning (const char *, const char *, ...)
	__attribute__ ((format (printf, 2, 3)));

/* Enable some more error checking.  */
int check_strict;
