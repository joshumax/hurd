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

/* Define this if memory objects should not be cached by the kernel.
   Normally, don't define it, but defining it causes a much greater rate
   of paging requests, which may be helpful in catching bugs. */

/* #undef DONT_CACHE_MEMORY_OBJECTS */


/* Simple reader/writer lock. */
struct rwlock
{
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

  struct mutex rwlock_master;
  struct condition rwlock_wakeup;

  struct rwlock dinlock;		/* locks INDIR_DOUBLE pointer */
  
  /* sinlock locks INDIR_SINGLE pointer and all the pointers in
     the double indir block.  */
  struct rwlock sinlock;		
  
  /* datalock locks all the direct block pointers and all the pointers
     in all the single indir blocks */
  struct rwlock datalock;

  /* These pointers are locked by sinmaplock and dinmaplock for all nodes. */
  daddr_t *dinloc;
  daddr_t *sinloc;
  
  /* These two pointers are locked by pagernplock in pager.c for
     all nodes. */
  struct user_pager_info *sininfo;
  struct user_pager_info *fileinfo;
};  

/* Get a reader lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_reader_lock (struct rwlock *lock, 
		    struct disknode *dn)
{
  mutex_lock (&dn->rwlock_master);
  if (lock->readers == -1 || lock->writers_waiting)
    {
      lock->readers_waiting++;
      do
	condition_wait (&dn->rwlock_wakeup, &dn->rwlock_master);
      while (lock->readers == -1 || lock->writers_waiting);
      lock->readers_waiting--;
    }
  lock->readers++;
  mutex_unlock (&dn->rwlock_master);
}

/* Get a writer lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_writer_lock (struct rwlock *lock,
		    struct disknode *dn)
{
  mutex_lock (&dn->rwlock_master);
  if (lock->readers)
    {
      lock->writers_waiting++;
      do
	condition_wait (&dn->rwlock_wakeup, &dn->rwlock_master);
      while (lock->readers);
      lock->writers_waiting--;
    }
  lock->readers = -1;
  mutex_unlock (&dn->rwlock_master);
}

/* Release a reader lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_reader_unlock (struct rwlock *lock,
		      struct disknode *dn)
{
  mutex_lock (&dn->rwlock_master);
  assert (lock->readers);
  lock->readers--;
  if (lock->readers_waiting || lock->writers_waiting)
    condition_broadcast (&dn->rwlock_wakeup);
  mutex_unlock (&dn->rwlock_master);
}

/* Release a writer lock on reader-writer lock LOCK for disknode DN */
extern inline void
rwlock_writer_unlock (struct rwlock *lock,
		      struct disknode *dn)
{
  mutex_lock (&dn->rwlock_master);
  assert (lock->readers == -1);
  lock->readers = 0;
  if (lock->readers_waiting || lock->writers_waiting)
    condition_broadcast (&dn->rwlock_wakeup);
  mutex_unlock (&dn->rwlock_master);
}

/* Initialize reader-writer lock LOCK */
extern inline void
rwlock_init (struct rwlock *lock)
{
  lock->readers = 0;
  lock->readers_waiting = 0;
  lock->writers_waiting = 0;
}

struct user_pager_info 
{
  struct node *np;
  enum pager_type 
    {
      DINODE,
      CG,
      SINDIR,
      DINDIR,
      FILE_DATA,
    } type;
  struct pager *p;
  struct user_pager_info *next, **prevp;
};

struct user_pager_info *dinpager, *dinodepager, *cgpager;

vm_address_t zeroblock;

struct fs *sblock;
struct dinode *dinodes;
vm_address_t cgs;
struct csum *csum;
int sblock_dirty;
int csum_dirty;
spin_lock_t alloclock;

struct mutex dinmaplock;
struct mutex sinmaplock;

spin_lock_t gennumberlock;
int nextgennumber;

mach_port_t ufs_device;

#define DEV_BSIZE 512
#define NBBY 8
#define btodb(n) ((n) / DEV_BSIZE)
#define howmany(x,y) (((x)+((y)-1))/(y))
#define roundup(x, y) ((((x)+((y)-1))/(y))*(y))
#define isclr(a, i) (((a)[(i)/NBBY] & (1<<((i)%NBBY))) == 0)
#define isset(a, i) ((a)[(i)/NBBY] & (1<<((i)%NBBY)))
#define setbit(a,i) ((a)[(i)/NBBY] |= 1<<((i)%NBBY))
#define clrbit(a,i) ((a)[(i)/NBBY] &= ~(1<<(i)%NBBY))

/* From alloc.c: */
error_t alloc (struct node *, daddr_t, daddr_t, int, daddr_t *, 
	       struct protid *);
void blkfree(volatile daddr_t bno, int size);
daddr_t blkpref (struct node *, daddr_t, int, daddr_t *);
error_t realloccg(struct node *, daddr_t, daddr_t,
		  int, int, daddr_t *, struct protid *);

/* From devio.c: */
error_t dev_write_sync (daddr_t addr, vm_address_t data, long len);
error_t dev_write (daddr_t addr, vm_address_t data, long len);
error_t dev_read_sync (daddr_t addr, vm_address_t *data, long len);

/* From hyper.c: */
void get_hypermetadata (void);

/* From inode.c: */
error_t iget (ino_t ino, struct node **NP);
struct node *ifind (ino_t ino);
void inode_init (void);
void write_all_disknodes (void);

/* From pager.c: */
void sync_dinode (struct node *, int);
void pager_init (void);
void din_map (struct node *);
void sin_map (struct node *);
void sin_remap (struct node *, int);
void sin_unmap (struct node *);
void din_unmap (struct node *);
void drop_pager_softrefs (struct node *);
void allow_pager_softrefs (struct node *);

/* From subr.c: */
void fragacct (int, long [], int);
int isblock(u_char *, daddr_t);
void clrblock(u_char *, daddr_t);
void setblock (u_char *, daddr_t);
int skpc (u_char, u_int, u_char *);
int scanc (u_int, u_char *, u_char [], u_char);
