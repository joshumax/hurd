/* 
   Copyright (C) 1994, 1995 Free Software Foundation

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
#include "fs.h"
#include "dinode.h"

/* Define this if memory objects should not be cached by the kernel.
   Normally, don't define it, but defining it causes a much greater rate
   of paging requests, which may be helpful in catching bugs. */

/* #undef DONT_CACHE_MEMORY_OBJECTS */

/* Simple reader/writer lock. */
struct rwlock
{
  struct mutex master;
  struct condition wakeup;
  int readers;
  int writers_waiting;
  int readers_waiting;
};

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

/* Get a reader lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_reader_lock (struct rwlock *lock)
{
  mutex_lock (&lock->master);
  if (lock->readers == -1 || lock->writers_waiting)
    {
      lock->readers_waiting++;
      do
	condition_wait (&lock->wakeup, &lock->master);
      while (lock->readers == -1 || lock->writers_waiting);
      lock->readers_waiting--;
    }
  lock->readers++;
  mutex_unlock (&lock->master);
}

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

/* Get a writer lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_writer_lock (struct rwlock *lock)
{
  mutex_lock (&lock->master);
  if (lock->readers)
    {
      lock->writers_waiting++;
      do
	condition_wait (&lock->wakeup, &lock->master);
      while (lock->readers);
      lock->writers_waiting--;
    }
  lock->readers = -1;
  mutex_unlock (&lock->master);
}

/* Release a reader lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_reader_unlock (struct rwlock *lock)
{
  mutex_lock (&lock->master);
  assert (lock->readers);
  lock->readers--;
  if (lock->readers_waiting || lock->writers_waiting)
    condition_broadcast (&lock->wakeup);
  mutex_unlock (&lock->master);
}

/* Release a writer lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_writer_unlock (struct rwlock *lock)
{
  mutex_lock (&lock->master);
  assert (lock->readers == -1);
  lock->readers = 0;
  if (lock->readers_waiting || lock->writers_waiting)
    condition_broadcast (&lock->wakeup);
  mutex_unlock (&lock->master);
}

/* Initialize reader-writer lock LOCK */
extern inline void
rwlock_init (struct rwlock *lock)
{
  mutex_init (&lock->master);
  condition_init (&lock->wakeup);
  lock->readers = 0;
  lock->readers_waiting = 0;
  lock->writers_waiting = 0;
}

struct user_pager_info 
{
  struct node *np;
  enum pager_type 
    {
      DISK,
      FILE_DATA,
    } type;
  struct pager *p;
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
char *ufs_device_name;

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

/* Convert an inode number to the dinode on disk. */
extern inline struct dinode *
dino (ino_t inum)
{
  return (struct dinode *)
    (disk_image 
     + fsaddr (sblock, ino_to_fsba (sblock, inum))
     + ino_to_fsbo (sblock, inum) * sizeof (struct dinode));
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

/* From devio.c: */
error_t dev_write_sync (daddr_t addr, vm_address_t data, long len);
error_t dev_write (daddr_t addr, vm_address_t data, long len);
error_t dev_read_sync (daddr_t addr, vm_address_t *data, long len);

/* From hyper.c: */
void get_hypermetadata (void);
void copy_sblock (void);

/* From inode.c: */
error_t iget (ino_t ino, struct node **NP);
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
