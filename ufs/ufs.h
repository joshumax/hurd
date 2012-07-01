/*
   Copyright (C) 1994, 1995, 1996, 1997, 1999 Free Software Foundation

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
#include <sys/mman.h>
#include <hurd/ports.h>
#include <hurd/pager.h>
#include <hurd/fshelp.h>
#include <hurd/iohelp.h>
#include <hurd/diskfs.h>
#include <sys/mman.h>
#include <assert.h>
#include <features.h>
#include "fs.h"
#include "dinode.h"

#ifdef UFS_DEFINE_EI
#define UFS_EI
#else
#define UFS_EI __extern_inline
#endif

/* Define this if memory objects should not be cached by the kernel.
   Normally, don't define it, but defining it causes a much greater rate
   of paging requests, which may be helpful in catching bugs. */

/* #undef DONT_CACHE_MEMORY_OBJECTS */

struct disknode
{
  ino_t number;

  int dir_idx;

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
  vm_prot_t max_prot;

  vm_offset_t allow_unlocked_pagein;
  vm_size_t unlocked_pagein_length;
};

#include <hurd/diskfs-pager.h>

/* The physical media.  */
extern struct store *store;
/* What the user specified.  */
extern struct store_parsed *store_parsed;

/* Mapped image of the disk.  */
extern void *disk_image;

extern void *zeroblock;

extern struct fs *sblock;
extern struct csum *csum;
int sblock_dirty;
int csum_dirty;

spin_lock_t node2pagelock;

spin_lock_t alloclock;

spin_lock_t gennumberlock;
u_long nextgennumber;

spin_lock_t unlocked_pagein_lock;

/* The compat_mode specifies whether or not we write
   extensions onto the disk. */
enum compat_mode
{
  COMPAT_GNU = 0,
  COMPAT_BSD42 = 1,
  COMPAT_BSD44 = 2,
} compat_mode;

/* If this is set, then this filesystem has two extensions:
   1) directory entries include the type field.
   2) symlink targets might be written directly in the di_db field
      of the dinode. */
int direct_symlink_extension;

/* If this is set, then the disk is byteswapped from native order. */
int swab_disk;

/* Number of device blocks per DEV_BSIZE block.  */
unsigned log2_dev_blocks_per_dev_bsize;

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

extern struct dinode * dino (ino_t inum);
extern daddr_t * indir_block (daddr_t bno);
extern struct cg * cg_locate (int ncg);
extern void sync_disk_blocks (daddr_t blkno, size_t nbytes, int wait);
extern void sync_dinode (int inum, int wait);
extern short swab_short (short arg);
extern long swab_long (long arg);
extern long long swab_long_long (long long arg);

#if defined(__USE_EXTERN_INLINES) || defined(UFS_DEFINE_EI)
/* Convert an inode number to the dinode on disk. */
UFS_EI struct dinode *
dino (ino_t inum)
{
  return (struct dinode *)
    (disk_image
     + fsaddr (sblock, ino_to_fsba (sblock, inum))
     + ino_to_fsbo (sblock, inum) * sizeof (struct dinode));
}

/* Convert a indirect block number to a daddr_t table. */
UFS_EI daddr_t *
indir_block (daddr_t bno)
{
  return (daddr_t *) (disk_image + fsaddr (sblock, bno));
}

/* Convert a cg number to the cylinder group. */
UFS_EI struct cg *
cg_locate (int ncg)
{
  return (struct cg *) (disk_image + fsaddr (sblock, cgtod (sblock, ncg)));
}

/* Sync part of the disk */
UFS_EI void
sync_disk_blocks (daddr_t blkno, size_t nbytes, int wait)
{
  pager_sync_some (diskfs_disk_pager, fsaddr (sblock, blkno), nbytes, wait);
}

/* Sync an disk inode */
UFS_EI void
sync_dinode (int inum, int wait)
{
  sync_disk_blocks (ino_to_fsba (sblock, inum), sblock->fs_fsize, wait);
}


/* Functions for byte swapping */
UFS_EI short
swab_short (short arg)
{
  return (((arg & 0xff) << 8)
	  | ((arg & 0xff00) >> 8));
}

UFS_EI long
swab_long (long arg)
{
  return (((long) swab_short (arg & 0xffff) << 16)
	  | swab_short ((arg & 0xffff0000) >> 16));
}

UFS_EI long long
swab_long_long (long long arg)
{
  return (((long long) swab_long (arg & 0xffffffff) << 32)
	  | swab_long ((arg & 0xffffffff00000000LL) >> 32));
}
#endif /* Use extern inlines.  */

/* Return ENTRY, after byteswapping it if necessary */
#define read_disk_entry(entry)						    \
({ 									    \
  typeof (entry) ret;							    \
  if (!swab_disk || sizeof (entry) == 1)				    \
    ret = (entry);							    \
  else if (sizeof (entry) == 2)					            \
    ret = swab_short (entry);						    \
  else if (sizeof (entry) == 4)					            \
    ret = swab_long (entry);						    \
  else									    \
    abort ();								    \
  ret;									    \
})

/* Execute A = B, but byteswap it along the way if necessary */
#define write_disk_entry(a,b)						    \
({									    \
  if (!swab_disk || sizeof (a) == 1)					    \
    ((a) = (b));							    \
  else if (sizeof (a) == 2)						    \
    ((a) = (swab_short (b)));						    \
  else if (sizeof (a) == 4)						    \
    ((a) = (swab_long (b)));						    \
  else									    \
    abort ();								    \
})





/* From alloc.c: */
error_t ffs_alloc (struct node *, daddr_t, daddr_t, int, daddr_t *,
		   struct protid *);
void ffs_blkfree(struct node *, daddr_t bno, long size);
daddr_t ffs_blkpref (struct node *, daddr_t, int, daddr_t *);
error_t ffs_realloccg(struct node *, daddr_t, daddr_t,
		  int, int, daddr_t *, struct protid *);

/* From bmap.c */
error_t fetch_indir_spec (struct node *, daddr_t, struct iblock_spec *);
void mark_indir_dirty (struct node *, daddr_t);

/* From hyper.c: */
void get_hypermetadata (void);
void copy_sblock (void);

/* From inode.c: */
struct node *ifind (ino_t ino);
void inode_init (void);
void write_all_disknodes (void);

/* From pager.c: */
void create_disk_pager (void);
void din_map (struct node *);
void sin_map (struct node *);
void sin_remap (struct node *, int);
void sin_unmap (struct node *);
void din_unmap (struct node *);
void drop_pager_softrefs (struct node *);
void allow_pager_softrefs (struct node *);
void flush_node_pager (struct node *);

/* From subr.c: */
void ffs_fragacct (struct fs *, int, long [], int);
int ffs_isblock(struct fs *, u_char *, daddr_t);
void ffs_clrblock(struct fs *, u_char *, daddr_t);
void ffs_setblock (struct fs *, u_char *, daddr_t);
int skpc (int, int, char *);
int scanc (u_int, u_char *, u_char [], int);

/* From pokeloc.c: */
void record_poke (void *, vm_size_t);
void sync_disk (int);
void flush_pokes ();
