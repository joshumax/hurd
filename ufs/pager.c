/* Pager for ufs
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

#include "ufs.h"
#include "dinode.h"
#include <strings.h>
#include <stdio.h>

spin_lock_t pagerlistlock = SPIN_LOCK_INITIALIZER;
struct user_pager_info *filepagerlist;

#ifdef DONT_CACHE_MEMORY_OBJECTS
#define MAY_CACHE 0
#else
#define MAY_CACHE 1
#endif

/* Find the location on disk of page OFFSET in pager UPI.  Return the
   disk address (in disk block) in *ADDR.  If *NPLOCK is set on
   return, then release that mutex after I/O on the data has
   completed.  Set DISKSIZE to be the amount of valid data on disk.
   (If this is an unallocated block, then set *ADDR to zero.)  */
static error_t
find_address (struct user_pager_info *upi,
	      vm_address_t offset,
	      daddr_t *addr,
	      int *disksize,
	      struct rwlock **nplock)
{
  assert (upi->type == DISK || upi->type == FILE_DATA);

  if (upi->type == DISK)
    {
      *disksize = __vm_page_size;
      *addr = offset / DEV_BSIZE;
      *nplock = 0;
      return 0;
    }
  else 
    {
      struct iblock_spec indirs[NINDIR + 1];
      struct node *np;
  
      np = upi->np;
      
      rwlock_reader_lock (&np->dn->allocptrlock);
      *nplock = &np->dn->allocptrlock;

      if (offset >= np->allocsize)
	{
	  rwlock_reader_unlock (&np->dn->allocptrlock);
	  return EIO;
	}
      
      if (offset + __vm_page_size > np->allocsize)
	*disksize = np->allocsize - offset;
      else
	*disksize = __vm_page_size;
      
      err = fetch_indir_spec (np, lblkno (offset), indirs);
      if (err)
	rwlock_reader_unlock (&np->dn->allocptrlock);
      else
	{
	  if (indirs[0].bno)
	    *addr = (fsbtodb (sblock, indirs[0].bno)
		     + blkoff (sblkc, offset) / DEV_BSIZE);
	  else
	    *addr = 0;
	}
      
      return err;
    }
}


/* Implement the pager_read_page callback from the pager library.  See 
   <hurd/pager.h> for the interface description. */
error_t
pager_read_page (struct user_pager_info *pager,
		 vm_offset_t page,
		 vm_address_t *buf,
		 int *writelock)
{
  error_t err;
  struct rwlock *nplock;
  daddr_t addr;
  int disksize;
  
  err = find_address (pager, page, &addr, &disksize, &nplock);
  if (err)
    return err;
  
  if (addr)
    {
      err = dev_read_sync (addr, (void *)buf, disksize);
      if (!err && disksize != __vm_page_size)
	bzero ((void *)(*buf + disksize), __vm_page_size - disksize);
      *writelock = 0;
    }
  else
    {
      printf ("Write-locked pagein Object %#x\tOffset %#x\n", pager, page);
      fflush (stdout);
      vm_allocate (mach_task_self (), buf, __vm_page_size, 1);
      *writelock = 1;
    }
      
  if (nplock)
    rwlock_reader_unlock (nplock);
  
  return err;
}

/* Implement the pager_write_page callback from the pager library.  See 
   <hurd/pager.h> for the interface description. */
error_t
pager_write_page (struct user_pager_info *pager,
		  vm_offset_t page,
		  vm_address_t buf)
{
  daddr_t addr;
  int disksize;
  struct rwlock *nplock;
  error_t err;
  
  err = find_address (pager, page, &addr, &disksize, &nplock);
  if (err)
    return err;
  
  if (addr)
    err = dev_write_sync (addr, buf, disksize);
  else
    {
      printf ("Attempt to write unallocated disk\n.");
      printf ("Object %#x\tOffset %#x\n", pager, page);
      fflush (stdout);
      err = 0;			/* unallocated disk; 
				   error would be pointless */
    }
    
  if (nplock)
    rwlock_reader_unlock (nplock);
  
  return err;
}

/* Implement the pager_unlock_page callback from the pager library.  See 
   <hurd/pager.h> for the interface description. */
error_t
pager_unlock_page (struct user_pager_info *pager,
		   vm_offset_t address)
{
  struct node *np;
  error_t err;
  struct iblock_spec indirs[NINDIR + 1];
  daddr_t bno;

  /* Zero an sblock->fs_bsize piece of disk starting at BNO, 
     synchronously.  We do this on newly allocated indirect
     blocks before setting the pointer to them to ensure that an
     indirect block absolutely never points to garbage. */
  void zero_disk_block (int bno)
    {
      bzero (indir_block (bno), sblock->fs_bsize);
      sync_disk_blocks (bno, sblock->fs_bsize, 1);
    };

  /* Problem--where to get cred values for allocation here? */

  printf ("Unlock page request, Object %#x\tOffset %#x...", pager, address);
  fflush (stdout);

  if (pager->type == DISK)
    return 0;
  
  np = pager->np;
  dn = np->dn;

  rwlock_writer_lock (&dn->allocptrlock);
  
  /* If this is the last block, we don't let it get unlocked. */
  if (address + __vm_page_size
      > blkroundup (sblock, np->allocsize) - sblock->fs_bsize)
    {
      printf ("attempt to unlock at last block denied\n");
      fflush (stdout);
      rwlock_writer_unlock (&np->dn->datalock, np->dn);
      return EIO;
    }
    
  err = fetch_indir_spec (np, lblkno (address), indirs);
  if (err)
    {
      rwlock_writer_unlock (&dn->allocptrlock);
      return EIO;
    }

  /* See if we need a triple indirect block; fail if we do. */
  assert (indirs[0].offset == -1 
	  || indirs[1].offset == -1 
	  || indirs[2].offset == -1);
  
  /* Check to see if this block is allocated. */
  if (indirs[0].bno == 0)
    {
      if (indirs[0].offset == -1)
	{
	  err = ffs_alloc (np, lblkno (address),
			   ffs_blkpref (np, lblkno (address),
					lblkno (address), di->di_db),
			   sblock->fs_bsize, &bno, 0);
	  if (err)
	    goto out;
	  assert (lblkno (address) < NDADDR);
	  indirs[0].bno = di->di_db[lblkno (address)] = bno;
	}
      else
	{
	  daddr_t *siblock;
	  
	  /* We need to set siblock to the single indirect block
	     array; see if the single indirect block is allocated. */
	  if (indirs[1].bno == 0)
	    {
	      if (indirs[1].offset == -1)
		{
		  err = ffs_alloc (np, lblkno (address),
				   ffs_blkpref (np, lblkno (address),
						INDIR_SINGLE, di->di_ib),
				   sblock->fs_bsize, &bno, 0);
		  if (err)
		    goto out;
		  zero_disk_block (bno);
		  indirs[1].bno = di->di_ib[INDIR_SINGLE] = bno;
		}
	      else
		{
		  daddr_t *diblock;
	      
		  /* We need to set diblock to the double indirect
		     block array; see if the double indirect block is
		     allocated. */
		  if (indirs[2].bno == 0)
		    {
		      /* This assert because triple indirection is
			 not supported. */
		      assert (indirs[2].offset == -1);
		      
		      err = ffs_alloc (np, lblkno (address),
				       ffs_blkpref (np, lblkno (address),
						    INDIR_DOUBLE, di->di_ib),
				       sblock->fs_bsize, &bno, 0);
		      if (err)
			goto out;
		      zero_disk_block (bno);
		      indirs[2].bno = di->di_ib[INDIR_DOUBLE] = bno;
		    }

		  diblock = indir_block (indirs[2].bno);
		  mark_indir_dirty (indirs[2].bno);
		  
		  /* Now we can allocate the single indirect block */
		  
		  err = ffs_alloc (np, lblkno (address),
				   ffs_blkpref (np, lblkno (address),
						indirs[1].offset, diblock),
				   sblock->fs_bsize, &bno, 0);
		  if (err)
		    goto out;
		  zero_disk_block (bno);
		  indirs[1].bno = diblock[indirs[1].offset] = bno;
		}
	    }
	  
	  siblock = indir_block (indirs[1].bno);
	  mark_indir_dirty (np, indirs[1].bno);

	  /* Now we can allocate the data block. */

	  err = ffs_alloc (np, lblkno (address),
			   ffs_blkpref (np, lblkno (address),
					indirs[0].offset, siblock),
			   sblock->fs_bsize, &bno, 0);
	  if (err)
	    goto out;
	  indirs[0].bno = siblock[indirs[0].offset] = bno;
	}
    }
  
 out:
  diskfs_end_catch_exception ();
  rwlock_writer_unlock (&np->dn->datalock, np->dn);
  return err;
}

/* Implement the pager_report_extent callback from the pager library.  See 
   <hurd/pager.h> for the interface description. */
inline error_t
pager_report_extent (struct user_pager_info *pager,
		     vm_address_t *offset,
		     vm_size_t *size)
{
  assert (pager->type == DISK || pager->type == FILE_DATA);

  *offset = 0;

  if (pager->type == DISK)
    *size = diskpagersize;
  else
    *size = pager->np->allocsize;
  
  return 0;
}

/* Implement the pager_clear_user_data callback from the pager library.
   See <hurd/pager.h> for the interface description. */
void
pager_clear_user_data (struct user_pager_info *upi)
{
  assert (upi->type == FILE_DATA);
  diskfs_nrele_light (upi->np);
  *upi->prevp = upi->next;
  if (upi->next)
    upi->next->prevp = upi->prevp;
  free (upi);
}



/* Create a the DISK pager, initializing DISKPAGER, and DISKPAGERPORT */
void
create_disk_pager ()
{
  diskpager = malloc (sizeof (struct user_pager_info));
  diskpager->type = DISK;
  diskpager->np = 0;
  diskpager->p = pager_create (upi, MAY_CACHE, MEMORY_OBJECT_COPY_NONE);
  diskpagerport = pager_get_port (upi->p);
  mach_port_insert_right (mach_task_self (), diskpagerport, diskpagerport,
			  MACH_MSG_TYPE_MAKE_SEND);
}  

/* This syncs a single file (NP) to disk.  Wait for all I/O to complete
   if WAIT is set.  NP->lock must be held.  */
void
diskfs_file_update (struct node *np,
		    int wait)
{
  struct indir_dirty *d, *tmp;
  
  if (np->dn->fileinfo)
    pager_sync (np->dn->fileinfo->p, wait);

  for (d = np->dn->dirty; d; d = tmp)
    {
      sync_disk_blocks (d->bno, sblock->fs_bsize, wait);
      tmp = d->next;
      free (d);
    }
  np->dn->dirty = 0;

  diskfs_node_update (np, wait);
}

/* Call this to create a FILE_DATA pager and return a send right.
   NP must be locked.  */
mach_port_t
diskfs_get_filemap (struct node *np)
{
  struct user_pager_info *upi;
  mach_port_t right;

  assert (S_ISDIR (np->dn_stat.st_mode)
	  || S_ISREG (np->dn_stat.st_mode)
	  || (S_ISLNK (np->dn_stat.st_mode)
	      && (!direct_symlink_extension 
		  || np->dn_stat.st_size >= sblock->fs_maxsymlinklen)));

  if (!np->dn->fileinfo)
    {
      upi = malloc (sizeof (struct user_pager_info));
      upi->type = FILE_DATA;
      upi->np = np;
      diskfs_nref_light (np);
      upi->p = pager_create (upi, MAY_CACHE, MEMORY_OBJECT_COPY_DELAY);
      np->dn->fileinfo = upi;
      ports_port_ref (p);

      spin_lock (&pagerlistlock);
      upi->next = filepagerlist;
      upi->prevp = &filepagerlist;
      if (upi->next)
	upi->next->prevp = &upi->next;
      filepagerlist = upi;
      spin_unlock (&pagerlistlock);
    }
  right = pager_get_port (np->dn->fileinfo->p);
  mutex_unlock (&pagernplock);
  mach_port_insert_right (mach_task_self (), right, right,
			  MACH_MSG_TYPE_MAKE_SEND);

  return right;
} 

/* Call this when we should turn off caching so that unused memory object
   ports get freed.  */
void
drop_pager_softrefs (struct node *np)
{
  if (MAY_CACHE && np->dn->fileinfo)
    pager_change_attributes (np->dn->fileinfo->p, 0,
			     MEMORY_OBJECT_COPY_DELAY, 0);
}

/* Call this when we should turn on caching because it's no longer
   important for unused memory object ports to get freed.  */
void
allow_pager_softrefs (struct node *np)
{
  if (MAY_CACHE && np->dn->fileinfo)
    pager_change_attributes (np->dn->fileinfo->p, 1,
			     MEMORY_OBJECT_COPY_DELAY, 0);
}

/* Call this to find out the struct pager * corresponding to the
   FILE_DATA pager of inode IP.  This should be used *only* as a subsequent
   argument to register_memory_fault_area, and will be deleted when 
   the kernel interface is fixed.  NP must be locked.  */
struct pager *
diskfs_get_filemap_pager_struct (struct node *np)
{
  return np->dn->fileinfo->p;
}

/* Call function FUNC (which takes one argument, a pager) on each pager, with
   all file pagers being processed before the disk pager.  Make the calls
   while holding no locks. */
static void
pager_traverse (void (*func)(struct user_pager_info *))
{
  struct user_pager_info *p;
  struct item {struct item *next; struct user_pager_info *p;} *list = 0;
  struct item *i;
  
  spin_lock (&pagerlistlock);
  for (p = filepagerlit; p; p = p->next)
    {
      i = alloca (sizeof (struct item));
      i->next = list;
      list = i;
      pager_reference (p->p);
      i->p = p;
    }
  spin_unlock (&pagerlistlock);
  
  for (i = list; i; i = i->next)
    {
      (*func)(i->p);
      pager_unreference (i->p->p);
    }
  
  (*func)(diskpager);
}

/* Shutdown all the pagers. */
void
diskfs_shutdown_pager ()
{
  void shutdown_one (struct user_pager_info *p)
    {
      pager_shutdown (p->p);
    }

  write_all_disknodes ();
  pager_traverse (shutdown_one);
}

/* Sync all the pagers. */
void
diskfs_sync_everything (int wait)
{
  void sync_one (struct user_pager_info *p)
    {
      pager_sync (p->p, wait);
    }
  
  write_all_disknodes ();
  pager_traverse (sync_one);
}
  
