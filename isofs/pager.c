/* 
   Copyright (C) 1997, 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <error.h>
#include <string.h>
#include "isofs.h"

spin_lock_t node2pagelock = SPIN_LOCK_INITIALIZER;

struct port_bucket *pager_bucket;

/* Mapped image of the disk */
void *disk_image;


/* Implement the read_page callback from the pager library.  See
   <hurd/pager.h> for the interface definition.  */
void
iso_read_pages (struct pager *pager, struct user_pager_info *upi,
                off_t start, off_t npages)
{
  void *buf;
  error_t err;
  daddr_t addr;
  size_t read = 0;
  int writelock = 1;   /* This is a read-only medium */
  size_t overrun = 0;
  struct node *np = upi->np;
  size_t size = npages * vm_page_size;
  vm_offset_t page = start * vm_page_size;

  if (upi->type == FILE_DATA)
    {
      addr = np->dn->file_start + (page >> store->log2_block_size);

      /* XXX ??? */
      if (page >= np->dn_stat.st_size)
	{
	  buf = mmap (0, size, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
          pager_data_supply (pager, 0 /* precious */, writelock,
                             start, npages, buf, 1 /* dealloc */);
	  return;
	}

      if (page + size > np->dn_stat.st_size)
	overrun = page + size - np->dn_stat.st_size;
    }
  else
    {
      assert (upi->type == DISK);
      addr = page >> store->log2_block_size;
    }

  err = store_read (store, addr, size, (void **) &buf, &read);
  if ((read != size) || err)
    {
      pager_data_read_error (pager, start, npages, err ? err : EIO);
      return;
    }

  if (overrun)
    memset (buf + size - overrun, 0, overrun);
    
  pager_data_supply (pager, 0 /* precious */, writelock,
                     start, npages, buf, 1 /* dealloc */);
}

/* Never permit unlocks to succeed. */
void
iso_unlock_pages (struct pager *pager,
                  struct user_pager_info *upi,
                  off_t start, off_t npages)
{
  pager_data_unlock_error (pager, start, npages, EROFS);
}

/* Tell how big the file is. */
void
iso_report_extent (struct user_pager_info *pager,
                   off_t *offset, off_t *size)
{
  *offset = 0;
  *size = pager->np->dn_stat.st_size;
}

/* Implement the pager_clear_user_data callback from the pager library. */
void
iso_clear_user_data (struct user_pager_info *upi)
{
  if (upi->type == FILE_DATA)
    {
      spin_lock (&node2pagelock);
      if (upi->np->dn->fileinfo == upi)
	upi->np->dn->fileinfo = 0;
      spin_unlock (&node2pagelock);
      diskfs_nrele_light (upi->np);
    }
}

struct pager_ops iso_ops =
  {
    .read = &iso_read_pages,
    .write = NULL,
    .unlock = &iso_unlock_pages,
    .report_extent = &iso_report_extent,
    .clear_user_data = &iso_clear_user_data,
    .dropweak = NULL
  };


/* Create the disk pager */
void
create_disk_pager (void)
{
  struct user_pager_info *upi;
  pager_bucket = ports_create_bucket ();
  diskfs_start_disk_pager (&iso_ops, sizeof (*upi), pager_bucket,
                           1, store->size, &disk_image);
  upi = pager_get_upi (diskfs_disk_pager);
  upi->type = DISK;
  upi->np = 0;
  upi->p = diskfs_disk_pager;
}

/* This need not do anything */
void
diskfs_file_update (struct node *np,
		    int wait)
{
}

/* Create a FILE_DATA pager for the specified node */
mach_port_t
diskfs_get_filemap (struct node *np, vm_prot_t prot)
{
  struct user_pager_info *upi;
  mach_port_t right;
  
  assert (S_ISDIR (np->dn_stat.st_mode)
	  || S_ISREG (np->dn_stat.st_mode)
	  || S_ISLNK (np->dn_stat.st_mode));
  
  spin_lock (&node2pagelock);
  
  do
    if (!np->dn->fileinfo)
      {
        struct pager *p;
        p = pager_create (&iso_ops, sizeof (*upi), pager_bucket,
                          1, MEMORY_OBJECT_COPY_DELAY);
	if (p == 0)
	  {
	    spin_unlock (&node2pagelock);
	    return MACH_PORT_NULL;
	  }
        upi = pager_get_upi (p);
	upi->type = FILE_DATA;
	upi->np = np;
	diskfs_nref_light (np);
	upi->p = p;
	np->dn->fileinfo = upi;
	right = pager_get_port (np->dn->fileinfo->p);
	ports_port_deref (np->dn->fileinfo->p);
      }
    else
      {
	/* Because NP->dn->fileinfo->p is not a real reference,
	   this might be nearly deallocated.  If that's so, then
	   the port right will be null.  In that case, clear here
	   and loop.  The deallocation will complete separately. */
	right = pager_get_port (np->dn->fileinfo->p);
	if (right == MACH_PORT_NULL)
	  np->dn->fileinfo = 0;
      }
  while (right == MACH_PORT_NULL);
  
  spin_unlock (&node2pagelock);
  
  mach_port_insert_right (mach_task_self (), right, right, 
			  MACH_MSG_TYPE_MAKE_SEND);
  
  return right;
}

/* Call this when we should turn off caching so that unused memory
   object ports get freed.  */
void
drop_pager_softrefs (struct node *np)
{
  struct user_pager_info *upi;

  spin_lock (&node2pagelock);
  upi = np->dn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  spin_unlock (&node2pagelock);

  if (upi)
    {
      pager_change_attributes (upi->p, 0, MEMORY_OBJECT_COPY_DELAY, 0);
      ports_port_deref (upi->p);
    }
}

/* Call this when we should turn on caching because it's no longer
   important for unused memory object ports to get freed.  */
void
allow_pager_softrefs (struct node *np)
{
  struct user_pager_info *upi;

  spin_lock (&node2pagelock);
  upi = np->dn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  spin_unlock (&node2pagelock);

  if (upi)
    {
      pager_change_attributes (upi->p, 1, MEMORY_OBJECT_COPY_DELAY, 0);
      ports_port_deref (upi->p);
    }
}
	
	
static void
block_caching ()
{
  error_t block_cache (void *arg)
    {
      struct pager *p = arg;

      pager_change_attributes (p, 0, MEMORY_OBJECT_COPY_DELAY, 1);
      return 0;
    }

  /* Loop through the pagers and turn off caching one by one,
     synchronously.  That should cause termination of each pager. */
  ports_bucket_iterate (pager_bucket, block_cache);
}
	  
static void
enable_caching ()
{
  error_t enable_cache (void *arg)
    {
      struct pager *p = arg;
      struct user_pager_info *upi = pager_get_upi (p);

      pager_change_attributes (p, 1, MEMORY_OBJECT_COPY_DELAY, 0);

      /* It's possible that we didn't have caching on before, because
	 the user here is the only reference to the underlying node
	 (actually, that's quite likely inside this particular
	 routine), and if that node has no links.  So dinkle the node
	 ref counting scheme here, which will cause caching to be
	 turned off, if that's really necessary.  */
      if (upi->type == FILE_DATA)
	{
	  diskfs_nref (upi->np);
	  diskfs_nrele (upi->np);
	}

      return 0;
    }

  ports_bucket_iterate (pager_bucket, enable_cache);
}

	    
/* Tell diskfs if there are pagers exported, and if none, then
   prevent any new ones from showing up.  */
int
diskfs_pager_users ()
{
  int npagers = ports_count_bucket (pager_bucket);

  if (npagers <= 1)
    return 0;

  block_caching ();

  /* Give it a second; the kernel doesn't actually shutdown
     immediately.  XXX */
  sleep (1);

  npagers = ports_count_bucket (pager_bucket);
  if (npagers <= 1)
    return 0;

  /* Darn, there are actual honest users.  Turn caching back on,
     and return failure. */
  enable_caching ();

  ports_enable_bucket (pager_bucket);

  return 1;
}

/* Return the bitwise or of the maximum prot parameter (the second arg to
   diskfs_get_filemap) for all active user pagers. */
vm_prot_t
diskfs_max_user_pager_prot ()
{
  /* We never allow writing, so there's no need to carefully check it. */
  return VM_PROT_READ | VM_PROT_EXECUTE;
}

/* Call this to find out the struct pager * corresponding to the
   FILE_DATA pager of inode IP.  This should be used *only* as a subsequent
   argument to register_memory_fault_area, and will be deleted when
   the kernel interface is fixed.  NP must be locked.  */
struct pager *
diskfs_get_filemap_pager_struct (struct node *np)
{
  /* This is safe because fileinfo can't be cleared; there must be
     an active mapping for this to be called. */
  return np->dn->fileinfo->p;
}

/* Shutdown all the pagers. */
void
diskfs_shutdown_pager ()
{
  /* Because there's no need to ever sync, we don't have to do anything
     here. */
}

/* Sync all the pagers. */
void
diskfs_sync_everything (int wait)
{
  /* ditto */
}
