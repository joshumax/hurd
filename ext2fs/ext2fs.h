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

#define IS_APPEND(node) ((node)->dn->info.i_flags & S_APPEND)
#define IS_IMMUTABLE(node) ((inode)->dn->info.i_flags & S_IMMUTABLE)

/* ---------------------------------------------------------------- */

/* The block size we assume the kernel device uses.  */
#define DEV_BSIZE 512

struct user_pager_info *disk_pager;
mach_port_t disk_pager_port;
void *disk_image;

char *device_name;
mach_port_t device_port;
off_t device_size;

/* Our in-core copy of the super-block.  */
struct ext2_super_block *sblock;
/* What to lock if changing the super block.  */
spin_lock_t sblock_lock;
/* True if sblock has been modified.  */
int sblock_dirty;

struct pokel sblock_pokel;

#define SBLOCK_BLOCK 1
#define SBLOCK_OFFS (SBLOCK_BLOCK * EXT2_MIN_BLOCK_SIZE)
#define SBLOCK_SIZE (sizeof (struct ext2_super_block))

/* The filesystem block-size.  */
unsigned long block_size;

vm_address_t zeroblock;

/* Copy the sblock into the disk.  */
void copy_sblock ();
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

spin_lock_t gennumberlock;
unsigned long nextgennumber;

/* ---------------------------------------------------------------- */
/* Functions for looking inside disk_image */

#define boffs(block) ((block) * block_size)
#define offsb(offs) ((block) / block_size)
#define offsaddr(offs) (((char *)disk_image) + (offs))
#define addroffs(offs) ((addr) - ((char *)disk_image))
#define baddr(block) offsaddr(boffs(block))
#define addrb(addr) offsb(addroffs(addr))

/* Get the descriptor for the block group inode INUM is in.  */
extern inline struct ext2_group_desc *
group_desc(unsigned long bg_num)
{
  int desc_per_block = EXT2_DESC_PER_BLOCK(sblock);
  unsigned long group_desc = bg_num / desc_per_block;
  unsigned long desc = bg_num % desc_per_block;
  return ((struct ext2_group_desc *)baddr(1 + group_desc)) + desc;
}

#define inode_group_num(inum) (((inum) - 1) / sblock->s_inodes_per_group)

/* Convert an inode number to the dinode on disk. */
extern inline struct ext2_inode *
dino (ino_t inum)
{
  unsigned long bg_num = inode_group_num(inum);
  struct ext2_group_desc *bg = group_desc(bg_num);
  unsigned long inodes_per_block = EXT2_INODES_PER_BLOCK(sblock);
  unsigned long block = bg->bg_inode_table + (bg_num / inodes_per_block);
  return ((struct ext2_inode *)baddr(block)) + inum % inodes_per_block;
}

/* Sync part of the disk */
extern inline void
sync_disk_image (void *place, size_t nbytes, int wait)
{
  pager_sync_some (disk_pager->p,
		   (char *)place - (char *)disk_image, nbytes, wait);
}

/* ---------------------------------------------------------------- */

/* Fetch inode INUM, set *NPP to the node structure; gain one user reference
   and lock the node.  */
error_t iget (ino_t inum, struct node **npp);

/* Lookup node INUM (which must have a reference already) and return it
   without allocating any new references. */
struct node *ifind (ino_t inum);

void inode_init (void);

/* ---------------------------------------------------------------- */

/* Sync all allocation information and node NP if diskfs_synchronous. */
inline void
alloc_sync (struct node *np)
{
  if (diskfs_synchronous)
    {
      if (np)
	{
	  diskfs_node_update (np, 1);
	  pokel_sync (&np->dn->pokel, 1);
	}
      copy_sblock ();
      diskfs_set_hypermetadata (1, 0);
      pokel_sync (&sblock_pokel, 1);
    }
}

/* ---------------------------------------------------------------- */

error_t ext2_getblk (struct node *node, long block, int create, char **buf);

int ext2_new_block (unsigned long goal,
		    u32 * prealloc_count, u32 * prealloc_block);

void ext2_free_blocks (unsigned long block, unsigned long count);

/* ---------------------------------------------------------------- */
/* Bitmap routines.  */

/* Returns TRUE if bit NUM is set in BITMAP.  */
inline int test_bit (unsigned num, char *bitmap)
{
  return bitmap[num >> 3] & (1 << (num & 0x7));
}

/* Sets bit NUM in BITMAP, and returns TRUE if it was already set.  */
inline int set_bit (unsigned num, char *bitmap)
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

/* Clears bit NUM in BITMAP, and returns TRUE if it was already clear.  */
inline int clear_bit (unsigned num, char *bitmap)
{
  char *p = bitmap + (num >> 3);
  char byte = *p;
  char mask = (1 << (num & 0x7));

  if (byte & mask)
    {
      *p = byte & ~mask;
      return 0;
    }
  else
    return 1;
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
