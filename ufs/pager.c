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
#include "fs.h"
#include "dinode.h"
#include <strings.h>
#include <stdio.h>

mach_port_t dinport;
mach_port_t dinodeport;
mach_port_t cgport;

/* Filesystem blocks of inodes per cylinder group */
static int infsb_pcg;

spin_lock_t pagerlistlock = SPIN_LOCK_INITIALIZER;
struct user_pager_info *filelist, *sinlist;

static void enqueue_pager (struct user_pager_info *);
static void dequeue_pager (struct user_pager_info *);
static daddr_t indir_alloc (struct node *, int, int);

/* Locks all nodes' sininfo and fileinfo fields. */
static struct mutex pagernplock = MUTEX_INITIALIZER;

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
	      struct rwlock **nplock,
	      struct disknode **dnp)
{
  int vblkno = lblkno (sblock, offset);
  int fsbaddr;
  struct node *volatile np;
  error_t err;
  struct mutex *maplock = 0;
  
  if (upi->type != FILE_DATA)
    *disksize = __vm_page_size;

  switch (upi->type)
    {
    default:
      assert (0);
      
    case CG:
      fsbaddr = cgtod (sblock, vblkno);
      *nplock = 0;
      break;
      
    case DINODE:
      fsbaddr = (cgimin (sblock, vblkno / infsb_pcg)
		 + blkstofrags (sblock, vblkno % infsb_pcg));
      *nplock = 0;
      break;
      
    case DINDIR:
      np = ifind (vblkno);

      rwlock_reader_lock (&np->dn->dinlock, np->dn);
      *nplock = &np->dn->dinlock;
      *dnp = np->dn;
      if (err = diskfs_catch_exception ())
	goto error;
      
      fsbaddr = dinodes[np->dn->number].di_ib[INDIR_DOUBLE];
      diskfs_end_catch_exception ();
      break;
      
    case SINDIR:
      np = upi->np;
      
      rwlock_reader_lock (&np->dn->sinlock, np->dn);
      *nplock = &np->dn->sinlock;
      *dnp = np->dn;

      if (err = diskfs_catch_exception ())
	goto error;

      if (vblkno == 0)
	fsbaddr = dinodes[np->dn->number].di_ib[INDIR_SINGLE];
      else
	{
	  mutex_lock (&dinmaplock);
	  maplock = &dinmaplock;
	  if (!np->dn->dinloc)
	    din_map (np);
	  fsbaddr = np->dn->dinloc[vblkno - 1];
	  mutex_unlock (&dinmaplock);
	}
      
      diskfs_end_catch_exception ();
      break;
      
    case FILE_DATA:
      np = upi->np;
      
      rwlock_reader_lock (&np->dn->datalock, np->dn);
      *nplock = &np->dn->datalock;
      *dnp = np->dn;

      if (offset >= np->allocsize)
	{
	  err = EIO;
	  goto error;
	}
      
      if (offset + __vm_page_size > np->allocsize)
	*disksize = np->allocsize - offset;
      else
	*disksize = __vm_page_size;
      
      if (err = diskfs_catch_exception ())
	goto error;
      
      if (vblkno < NDADDR)
	fsbaddr = dinodes[np->dn->number].di_db[vblkno];
      else
	{
	  mutex_lock (&sinmaplock);
	  maplock = &sinmaplock;
	  if (!np->dn->sinloc)
	    sin_map (np);
	  fsbaddr = np->dn->sinloc[vblkno - NDADDR];
	  mutex_unlock (&sinmaplock);
	}
      diskfs_end_catch_exception ();
      break;
    }

  if (fsbaddr)
    *addr = fsbtodb (sblock, fsbaddr) + blkoff (sblock, offset) / DEV_BSIZE;
  else
    *addr = 0;
   
  return 0;

 error:
  if (*nplock)
    rwlock_reader_unlock (*nplock, *dnp);
  if (maplock)
    mutex_unlock (maplock);
  return err;
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
  struct disknode *dn;
  
  err = find_address (pager, page, &addr, &disksize, &nplock, &dn);
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
      vm_allocate (mach_task_self (), buf, __vm_page_size, 1);
      *writelock = 1;
    }
      
  if (nplock)
    rwlock_reader_unlock (nplock, dn);
  
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
  struct disknode *dn;
  
  err = find_address (pager, page, &addr, &disksize, &nplock, &dn);
  if (err)
    return err;
  
  if (addr)
    err = dev_write_sync (addr, buf, disksize);
  else
    {
      printf ("Attempt to write unallocated disk\n.");
      err = 0;			/* unallocated disk; 
				   error would be pointless */
    }
    
  if (nplock)
    rwlock_reader_unlock (nplock, dn);
  
  return err;
}

/* Implement the pager_unlock_page callback from the pager library.  See 
   <hurd/pager.h> for the interface description. */
error_t
pager_unlock_page (struct user_pager_info *pager,
		   vm_offset_t address)
{
  struct node *volatile np;
  error_t err;
  daddr_t vblkno;
  daddr_t *slot, *table;
  daddr_t newblk;

  /* Problem--where to get cred values for allocation here? */

  vblkno = address / sblock->fs_bsize;

  switch (pager->type)
    {
    case DINDIR:
      np = ifind (vblkno);
      
      rwlock_writer_lock (&np->dn->dinlock, np->dn);
      if (diskfs_catch_exception ())
	err =  EIO;
      else
	{
	  if (dinodes[np->dn->number].di_ib[INDIR_DOUBLE])
	    err = 0;
	  else
	    {
	      newblk = indir_alloc (np, INDIR_DOUBLE, 0);
	      if (newblk)
		{
		  dinodes[np->dn->number].di_ib[INDIR_DOUBLE] = newblk;
		  err = 0;
		}
	      else
		err = ENOSPC;
	    }
	  diskfs_end_catch_exception ();
	}
      rwlock_writer_unlock (&np->dn->dinlock, np->dn);
      break;
      
    case SINDIR:
      np = pager->np;
      rwlock_writer_lock (&np->dn->sinlock, np->dn);
      
      if (diskfs_catch_exception ())
	err = EIO;
      else
	{
	  mutex_lock (&dinmaplock);
	  if (vblkno == 0)
	    slot = &dinodes[np->dn->number].di_ib[INDIR_SINGLE];
	  else
	    {
	      if (!np->dn->dinloc)
		din_map (np);
	      slot = &np->dn->dinloc[vblkno - 1];
	    }

	  if (*slot)
	    err = 0;
	  else
	    {
	      newblk = indir_alloc (np, INDIR_SINGLE, vblkno);
	      if (newblk)
		{
		  *slot = newblk;
		  err = 0;
		}
	      else
		err = ENOSPC;
	    }
	  mutex_unlock (&dinmaplock);
	  diskfs_end_catch_exception ();
	}
      rwlock_writer_unlock (&np->dn->sinlock, np->dn);
      break;

    case FILE_DATA:
      np = pager->np;
      rwlock_writer_lock (&np->dn->datalock, np->dn);

      /* If this is the last block, we don't let it get unlocked. */
      if (address + __vm_page_size
	  > blkroundup (sblock, np->allocsize) - sblock->fs_bsize)
	{
	  printf ("attempt to unlock at last block denied\n");
	  rwlock_writer_unlock (&np->dn->datalock, np->dn);
	  return EIO;
	}
      
      if (diskfs_catch_exception ())
	err = EIO;
      else
	{
	  mutex_lock (&sinmaplock);
	  if (vblkno < NDADDR)
	    {
	      slot = &dinodes[np->dn->number].di_db[vblkno];
	      table = dinodes[np->dn->number].di_db;
	    }
	  else
	    {
	      if (!np->dn->sinloc)
		sin_map (np);
	      slot = &np->dn->sinloc[vblkno - NDADDR];
	      table = np->dn->sinloc;
	    }
	  
	  if (*slot)
	    err = 0;
	  else
	    {
	      ffs_alloc (np, vblkno, 
			 ffs_blkpref (np, vblkno, slot - table, table),
			 sblock->fs_bsize, &newblk, 0);
	      if (newblk)
		{
		  *slot = newblk;
		  err = 0;
		}
	      else
		err = ENOSPC;
	    }
	  mutex_unlock (&sinmaplock);
	  diskfs_end_catch_exception ();
	}
      rwlock_writer_unlock (&np->dn->datalock, np->dn);
      break;
      
    default:
      err = 0;
    }
  
  return err;
}

/* Implement the pager_report_extent callback from the pager library.  See 
   <hurd/pager.h> for the interface description. */
inline error_t
pager_report_extent (struct user_pager_info *pager,
		     vm_address_t *offset,
		     vm_size_t *size)
{
  *offset = 0;
  switch (pager->type)
    {
    case DINODE:
      *size = sblock->fs_ipg * sblock->fs_ncg * sizeof (struct dinode);
      break;
      
    case CG:
      *size = sblock->fs_bsize * sblock->fs_ncg;
      break;
      
    case DINDIR:
      *size = sblock->fs_ipg * sblock->fs_ncg * sblock->fs_bsize;
      break;

    case SINDIR:
      {
	int sizet;

	/* sizet = disk size of the file */
	sizet = pager->np->allocsize;

	/* sizet = number of fs blocks in file */
	sizet = (sizet + sblock->fs_bsize - 1) / sblock->fs_bsize;

	/* sizet = number of fs blocks not list in di_db */
	sizet -= NDADDR;

	/* sizet = space to hold that many pointers */
	sizet *= sizeof (daddr_t);

	/* And that's the size of the sindir area for the file. */
	*size = sizet;
      }
      break;
      
    case FILE_DATA:
      *size = pager->np->allocsize;
      break;
    }
  
  *size = round_page (*size);
  return 0;
}

/* Implement the pager_clear_user_data callback from the pager library.
   See <hurd/pager.h> for the interface description. */
void
pager_clear_user_data (struct user_pager_info *upi)
{
  struct node *np = upi->np;

  switch (upi->type)
    {
    case FILE_DATA:
      mutex_lock (&sinmaplock);
      mutex_lock (&pagernplock);
      np->dn->fileinfo = 0;
      mutex_unlock (&pagernplock);
      if (np->dn->sinloc)
	sin_unmap (np);
      mutex_unlock (&sinmaplock);
      break;
      
    case SINDIR:
      mutex_lock (&dinmaplock);
      mutex_lock (&pagernplock);
      np->dn->sininfo = 0;
      mutex_unlock (&pagernplock);
      if (np->dn->dinloc)
	din_unmap (np);
      mutex_unlock (&dinmaplock);
      break;
      
    case DINDIR:
      dinpager = 0;
      return;
    case CG:
      cgpager = 0;
      return;
    case DINODE:
      dinodepager = 0;
      return;
    }

  if (np)
    diskfs_nrele_light (np);
  dequeue_pager (upi);
  free (upi);
}


/* This is called (with sinmaplock held) to map the contents of the
   single indirect blocks of node NP. */
void
sin_map (struct node *np)
{
  int err;
  struct user_pager_info *upi;
  mach_port_t port;
  vm_address_t offset;
  vm_size_t extent;
  
  assert (!np->dn->sinloc);

  mutex_lock (&pagernplock);
  if (np->dn->sininfo)
    {
      upi = np->dn->sininfo;
      port = pager_get_port (upi->p);
      mach_port_insert_right (mach_task_self (), port, port,
			      MACH_MSG_TYPE_MAKE_SEND);
    }
  else
    {
      upi = malloc (sizeof (struct user_pager_info));
      upi->type = SINDIR;
      upi->np = np;
      diskfs_nref_light (np);

      upi->p = pager_create (upi, MAY_CACHE, MEMORY_OBJECT_COPY_NONE);
      np->dn->sininfo = upi;
      enqueue_pager (upi);
      port = pager_get_port (upi->p);
      mach_port_insert_right (mach_task_self (), port, port,
			      MACH_MSG_TYPE_MAKE_SEND); 
    }
  mutex_unlock (&pagernplock);
  
  pager_report_extent (upi, &offset, &extent);
  
  err = vm_map (mach_task_self (), (vm_address_t *)&np->dn->sinloc, 
		extent, 0, 1, port, offset, 0, VM_PROT_READ|VM_PROT_WRITE,
		VM_PROT_READ|VM_PROT_WRITE, VM_INHERIT_NONE);
  mach_port_deallocate (mach_task_self (), port);
  
  assert (!err);
  
  diskfs_register_memory_fault_area (np->dn->sininfo->p, offset, 
				     np->dn->sinloc, extent);
}

/* This is caled when a file (NP) grows (to size NEWSIZE) to see
   if the single indirect mapping needs to grow to.  sinmaplock
   must be held.
   The caller must set ip->i_allocsize to reflect newsize. */
void
sin_remap (struct node *np,
	   int newsize)
{
  struct user_pager_info *upi;
  int err;
  vm_address_t offset;
  vm_size_t size;
  mach_port_t port;

  mutex_lock (&pagernplock);
  upi = np->dn->sininfo;

  pager_report_extent (upi, &offset, &size);
  
  /* This is the same calculation as in pager_report_extent
     for the SINDIR case.  */
  newsize = (newsize + sblock->fs_bsize - 1) / sblock->fs_bsize;
  newsize -= NDADDR;
  newsize *= sizeof (daddr_t);
  newsize = round_page (newsize);
 
  assert (newsize >= size);
  if (newsize != size)
    {
      diskfs_unregister_memory_fault_area (np->dn->sinloc, size);
      vm_deallocate (mach_task_self (), (u_int) np->dn->sinloc, size);
      
      port = pager_get_port (upi->p);
      mach_port_insert_right (mach_task_self (), port, port,
			      MACH_MSG_TYPE_MAKE_SEND);
      err = vm_map (mach_task_self (), (u_int *)&np->dn->sinloc, size,
		    0, 1, port, 0, 0, VM_PROT_READ|VM_PROT_WRITE,
		    VM_PROT_READ|VM_PROT_WRITE, VM_INHERIT_NONE);
      mach_port_deallocate (mach_task_self (), port);
      assert (!err);
      diskfs_register_memory_fault_area (np->dn->sininfo->p, 0,
					 np->dn->sinloc, size);
    }
  mutex_unlock (&pagernplock);
}

/* This is called (with sinmaplock set) to unmap the
   single indirect block mapping of node NP. */
void
sin_unmap (struct node *np)
{
  vm_offset_t start;
  vm_size_t len;
  
  assert (np->dn->sinloc);
  pager_report_extent (np->dn->sininfo, &start, &len);
  diskfs_unregister_memory_fault_area (np->dn->sinloc, len);
  vm_deallocate (mach_task_self (), (u_int) np->dn->sinloc, len);
  np->dn->sinloc = 0;
}

/* This is called (with dinmaplock set) to map the contents
   of the double indirect block of node NP. */
void
din_map (struct node *np)
{
  int err;
  
  assert (!np->dn->dinloc);
  
  err = vm_map (mach_task_self (), (vm_address_t *)&np->dn->dinloc,
		sblock->fs_bsize, 0, 1, dinport, 
		np->dn->number * sblock->fs_bsize, 0,
		VM_PROT_READ|VM_PROT_WRITE, VM_PROT_READ|VM_PROT_WRITE,
		VM_INHERIT_NONE);
  assert (!err);
  diskfs_register_memory_fault_area (dinpager->p,
				     np->dn->number * sblock->fs_bsize,
				     np->dn->dinloc, sblock->fs_bsize);
}

/* This is called (with dinmaplock set) to unmap the double
   indirect block mapping of node NP. */
void
din_unmap (struct node *np)
{
  diskfs_unregister_memory_fault_area (np->dn->dinloc, sblock->fs_bsize);
  vm_deallocate (mach_task_self (), (u_int) np->dn->dinloc, sblock->fs_bsize);
  np->dn->dinloc = 0;
}

/* Initialize the pager subsystem. */
void
pager_init ()
{
  struct user_pager_info *upi;
  vm_address_t offset;
  vm_size_t size;
  error_t err;

    /* firewalls: */
  assert ((DEV_BSIZE % sizeof (struct dinode)) == 0);
  assert ((__vm_page_size % DEV_BSIZE) == 0);
  assert ((sblock->fs_bsize % DEV_BSIZE) == 0);
  assert ((sblock->fs_ipg % sblock->fs_inopb) == 0);
  assert (__vm_page_size <= sblock->fs_bsize);

  infsb_pcg = sblock->fs_ipg / sblock->fs_inopb;

  vm_allocate (mach_task_self (), &zeroblock, sblock->fs_bsize, 1);

  upi = malloc (sizeof (struct user_pager_info));
  upi->type = DINODE;
  upi->np = 0;
  upi->p = pager_create (upi, MAY_CACHE, MEMORY_OBJECT_COPY_NONE);
  dinodepager = upi;
  dinodeport = pager_get_port (upi->p);
  mach_port_insert_right (mach_task_self (), dinodeport, dinodeport,
			  MACH_MSG_TYPE_MAKE_SEND);
  pager_report_extent (upi, &offset, &size);
  err = vm_map (mach_task_self (), (vm_address_t *)&dinodes, size,
		0, 1, dinodeport, offset, 0, VM_PROT_READ|VM_PROT_WRITE,
		VM_PROT_READ|VM_PROT_WRITE, VM_INHERIT_NONE);
  assert (!err);
  diskfs_register_memory_fault_area (dinodepager->p, 0, dinodes, size);
  
  upi = malloc (sizeof (struct user_pager_info));
  upi->type = CG;
  upi->np = 0;
  upi->p = pager_create (upi, MAY_CACHE, MEMORY_OBJECT_COPY_NONE);
  cgpager = upi;
  cgport = pager_get_port (upi->p);
  mach_port_insert_right (mach_task_self (), cgport, cgport,
			  MACH_MSG_TYPE_MAKE_SEND);
  pager_report_extent (upi, &offset, &size);
  err = vm_map (mach_task_self (), &cgs, size,
		0, 1, cgport, offset, 0, VM_PROT_READ|VM_PROT_WRITE,
		VM_PROT_READ|VM_PROT_WRITE, VM_INHERIT_NONE);
  assert (!err);
  diskfs_register_memory_fault_area (cgpager->p, 0, (void *)cgs, size);

  upi = malloc (sizeof (struct user_pager_info));
  upi->type = DINDIR;
  upi->np = 0;
  upi->p = pager_create (upi, MAY_CACHE, MEMORY_OBJECT_COPY_NONE);
  dinpager = upi;
  dinport = pager_get_port (upi->p);
  mach_port_insert_right (mach_task_self (), dinport, dinport,
			  MACH_MSG_TYPE_MAKE_SEND);
}

/* Allocate one indirect block for NP.  TYPE is either INDIR_DOUBLE or
   INDIR_SINGLE; IND is (for INDIR_SINGLE) the index of the block
   (The first block is 0, the next 1, etc.).  */
static daddr_t
indir_alloc (struct node *np,
	     int type,
	     int ind)
{
  daddr_t bn;
  daddr_t lbn;
  int error;

  switch (type)
    {
    case INDIR_DOUBLE:
      lbn = NDADDR + sblock->fs_bsize / sizeof (daddr_t);
      break;
    case INDIR_SINGLE:
      if (ind == 0)
	lbn = NDADDR;
      else
	lbn = NDADDR + ind * sblock->fs_bsize / sizeof (daddr_t);
      break;
    default:
      assert (0);
    }
  
  if (error = ffs_alloc (np, NDADDR,
			 ffs_blkpref (np, lbn, 0, (daddr_t *)0),
			 sblock->fs_bsize, &bn, 0))
    return 0;

  /* We do this write synchronously so that the inode never
     points at an indirect block full of garbage */
  if (dev_write_sync (fsbtodb (sblock, bn), zeroblock, sblock->fs_bsize))
    {
      ffs_blkfree (np, bn, sblock->fs_bsize);
      return 0;
    }
  else
    return bn;
}

/* Write a single dinode (NP->dn->number) to disk.  This might sync more 
   than actually necessary; it's really just an attempt to avoid syncing 
   all the inodes.  Return immediately if WAIT is clear. */
void
sync_dinode (struct node *np,
	     int wait)
{
  vm_offset_t offset, offsetpg;
  
  offset = np->dn->number * sizeof (struct dinode);
  offsetpg = offset / __vm_page_size;
  offset = offsetpg * __vm_page_size;

  pager_sync_some (dinodepager->p, offset, __vm_page_size, wait);
}

/* This syncs a single file (NP) to disk.  Wait for all I/O to complete
   if WAIT is set.  NP->lock must be held.  */
void
diskfs_file_update (struct node *np,
		    int wait)
{
  mutex_lock (&pagernplock);
  if (np->dn->fileinfo)
    pager_sync (np->dn->fileinfo->p, wait);
  mutex_unlock (&pagernplock);

  mutex_lock (&pagernplock);
  if (np->dn->sininfo)
      pager_sync (np->dn->sininfo->p, wait);
  mutex_unlock (&pagernplock);

  pager_sync_some (dinpager->p, np->dn->number * sblock->fs_bsize,
		   sblock->fs_bsize, wait);

  diskfs_node_update (np, wait);
}

/* Call this to create a FILE_DATA pager and return a send right.
   NP must be locked.  The toplock must be locked. */
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

  mutex_lock (&pagernplock);
  if (!np->dn->fileinfo)
    {
      upi = malloc (sizeof (struct user_pager_info));
      upi->type = FILE_DATA;
      upi->np = np;
      diskfs_nref_light (np);
      upi->p = pager_create (upi, MAY_CACHE, MEMORY_OBJECT_COPY_DELAY);
      np->dn->fileinfo = upi;
      enqueue_pager (upi);
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
  if (MAY_CACHE)
    {
      mutex_lock (&pagernplock);
      if (np->dn->fileinfo)
	pager_change_attributes (np->dn->fileinfo->p, 0,
				 MEMORY_OBJECT_COPY_DELAY, 0);
      if (np->dn->sininfo)
	pager_change_attributes (np->dn->sininfo->p, 0,
				 MEMORY_OBJECT_COPY_DELAY, 0);
      mutex_unlock (&pagernplock);
    }
}

/* Call this when we should turn on caching because it's no longer
   important for unused memory object ports to get freed.  */
void
allow_pager_softrefs (struct node *np)
{
  if (MAY_CACHE)
    {
      mutex_lock (&pagernplock);
      if (np->dn->fileinfo)
	pager_change_attributes (np->dn->fileinfo->p, 1,
				 MEMORY_OBJECT_COPY_DELAY, 0);
      if (np->dn->sininfo)
	pager_change_attributes (np->dn->sininfo->p, 1,
				 MEMORY_OBJECT_COPY_DELAY, 0);
    }
}

/* Call this to find out the struct pager * corresponding to the
   FILE_DATA pager of inode IP.  This should be used *only* as a subsequent
   argument to register_memory_fault_area, and will be deleted when 
   the kernel interface is fixed. */
struct pager *
diskfs_get_filemap_pager_struct (struct node *np)
{
  struct pager *p;
  mutex_unlock (&pagernplock);
  p = np->dn->fileinfo->p;
  mutex_unlock (&pagernplock);
  return p;
}

/* Add pager P to the appropriate list (filelist or sinlist) of pagers
   of its type.  */
static void
enqueue_pager (struct user_pager_info *p)
{
  struct user_pager_info **listp;

  if (p->type == FILE_DATA)
    listp = &filelist;
  else if (p->type == SINDIR)
    listp = &filelist;
  else
    return;
  
  spin_lock (&pagerlistlock);
  
  p->next = *listp;
  p->prevp = listp;
  *listp = p;
  if (p->next)
    p->next->prevp = &p->next;
  
  spin_unlock (&pagerlistlock);
}

/* Remove pager P from the linked list it was placed on with enqueue_pager. */
static void
dequeue_pager (struct user_pager_info *p)
{
  spin_lock (&pagerlistlock);
  if (p->next)
    p->next->prevp = p->prevp;
  *p->prevp = p->next;
  spin_unlock (&pagerlistlock);
}

/* Call function FUNC (which takes one argument, a pager) on each pager, with
   all file pagers being processed before sindir pagers, and then the dindir,
   dinode, and cg pagers (in that order).  Make the calls while holding
   no locks.  */
static void
pager_traverse (void (*func)(struct user_pager_info *))
{
  struct user_pager_info *p;
  struct item {struct item *next; struct user_pager_info *p;} *list = 0;
  struct item *i;
  int looped;
  
  /* Putting SINDIR's on first means they will be removed last; after
  the FILE_DATA pagers.  */
  spin_lock (&pagerlistlock);
  for (p = sinlist, looped = 0;
       p || (!looped && (looped = 1, p = filelist));
       p = p->next)
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
  
  (*func)(dinpager);
  (*func)(dinodepager);
  (*func)(cgpager);
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
  
