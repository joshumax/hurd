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

pthread_spinlock_t node2pagelock = PTHREAD_SPINLOCK_INITIALIZER;

struct port_bucket *pager_bucket;

/* Mapped image of the disk */
void *disk_image;
size_t disk_image_len;


/* Implement the pager_read_page callback from the pager library.  See
   <hurd/pager.h> for the interface definition.  */
error_t
pager_read_page (struct user_pager_info *upi,
		 vm_offset_t page,
		 vm_address_t *buf,
		 int *writelock)
{
  error_t err;
  daddr_t addr;
  struct node *np = upi->np;
  size_t read = 0;
  size_t overrun = 0;

  /* This is a read-only medium */
  *writelock = 1;

  if (upi->type == FILE_DATA)
    {
      addr = np->dn->file_start + (page >> store->log2_block_size);
  
      if (page >= np->dn_stat.st_size)
	{
	  *buf = (vm_address_t) mmap (0, vm_page_size, PROT_READ|PROT_WRITE,
				      MAP_ANON, 0, 0);
	  return 0;
	}

      if (page + vm_page_size > np->dn_stat.st_size)
	overrun = page + vm_page_size - np->dn_stat.st_size;
    }
  else
    {
      assert_backtrace (upi->type == DISK);
      addr = page >> store->log2_block_size;
    }

  err = store_read (store, addr, vm_page_size, (void **) buf, &read);
  if (err)
    return err;

  if (read != vm_page_size)
    return EIO;

  if (overrun)
    memset ((void *)*buf + vm_page_size - overrun, 0, overrun);
    
  return 0;
}

/* This function should never be called.  */
error_t
pager_write_page (struct user_pager_info *pager,
		  vm_offset_t page,
		  vm_address_t buf)
{
  assert_backtrace (0);
}

/* Never permit unlocks to succeed. */
error_t
pager_unlock_page (struct user_pager_info *pager,
		   vm_offset_t address)
{
  return EROFS;
}

void
pager_notify_evict (struct user_pager_info *pager,
		    vm_offset_t page)
{
  assert_backtrace (!"unrequested notification on eviction");
}

/* Tell how big the file is. */
error_t
pager_report_extent (struct user_pager_info *pager,
		     vm_address_t *offset,
		     vm_size_t *size)
{
  *offset = 0;
  *size = pager->np->dn_stat.st_size;
  return 0;
}

/* Implement the pager_clear_user_data callback from the pager library. */
void
pager_clear_user_data (struct user_pager_info *upi)
{
  if (upi->type == FILE_DATA)
    {
      pthread_spin_lock (&node2pagelock);
      if (upi->np->dn->fileinfo == upi)
	upi->np->dn->fileinfo = 0;
      pthread_spin_unlock (&node2pagelock);
      diskfs_nrele_light (upi->np);
    }
}

void
pager_dropweak (struct user_pager_info *upi)
{
}


/* Create the disk pager */
void
create_disk_pager (void)
{
  struct user_pager_info *upi = malloc (sizeof (struct user_pager_info));

  if (!upi)
    error (1, errno, "Could not create disk pager");
  upi->type = DISK;
  upi->np = 0;
  pager_bucket = ports_create_bucket ();
  diskfs_start_disk_pager (upi, pager_bucket, 1, 0, store->size, &disk_image);
  disk_image_len = store->size;
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
  
  assert_backtrace (S_ISDIR (np->dn_stat.st_mode)
	  || S_ISREG (np->dn_stat.st_mode)
	  || S_ISLNK (np->dn_stat.st_mode));
  
  pthread_spin_lock (&node2pagelock);
  
  do
    if (!np->dn->fileinfo)
      {
        struct pager *p;
        p = pager_create_alloc (sizeof *upi, pager_bucket, 1,
                                MEMORY_OBJECT_COPY_DELAY, 0);
	if (p == NULL)
	  {
	    diskfs_nrele_light (np);
	    pthread_spin_unlock (&node2pagelock);
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
  
  pthread_spin_unlock (&node2pagelock);
  
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

  pthread_spin_lock (&node2pagelock);
  upi = np->dn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  pthread_spin_unlock (&node2pagelock);

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

  pthread_spin_lock (&node2pagelock);
  upi = np->dn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  pthread_spin_unlock (&node2pagelock);

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
