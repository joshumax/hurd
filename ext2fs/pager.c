/* Pager for ext2fs

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

#include <strings.h>
#include "ext2fs.h"

spin_lock_t pager_list_lock = SPIN_LOCK_INITIALIZER;
struct user_pager_info *file_pager_list;

spin_lock_t node_to_page_lock = SPIN_LOCK_INITIALIZER;

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
	      int *length,
	      struct rwlock **nplock)
{
  assert (upi->type == DISK || upi->type == FILE_DATA);

  if (upi->type == DISK)
    {
      *length = vm_page_size;
      *addr = offset / DEV_BSIZE;
      *nplock = 0;
      return 0;
    }
  else 
    {
      error_t err;
      struct node *np = upi->np;
      
      rwlock_reader_lock (&np->dn->alloc_lock);
      *nplock = &np->dn->alloc_lock;

      if (offset >= np->allocsize)
	{
	  rwlock_reader_unlock (&np->dn->alloc_lock);
	  return EIO;
	}
      
      if (offset + vm_page_size > np->allocsize)
	*length = np->allocsize - offset;
      else
	*length = vm_page_size;

      err = ext2_getblk(np, offset / block_size, 0, addr);
      if (err == EINVAL)
	{
	  *addr = 0;
	  err = 0;
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
  int length;
  
  err = find_address (pager, page, &addr, &length, &nplock);
  if (err)
    return err;
  
  if (addr)
    {
      err = dev_read_sync (addr, (void *)buf, length);
      if (!err && length != vm_page_size)
	bzero ((void *)(*buf + length), vm_page_size - length);
      *writelock = 0;
    }
  else
    {
      vm_allocate (mach_task_self (), buf, vm_page_size, 1);
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
  int length;
  struct rwlock *nplock;
  error_t err;
  
  err = find_address (pager, page, &addr, &length, &nplock);
  if (err)
    return err;
  
  if (addr)
    err = dev_write_sync (addr, buf, length);
  else
    {
      ext2_error("pager_write_page",
		 "Attempt to write unallocated disk;"
		 " object = %p; offset = 0x%x", pager, page);
      /* unallocated disk; error would be pointless */
      err = 0;
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
  if (pager->type == DISK)
    return 0;
  else
    {
      error_t err;
      char *buf;
      struct node *np = pager->np;
      struct disknode *dn = np->dn;

      rwlock_writer_lock (&dn->alloc_lock);

      err = diskfs_catch_exception ();
      if (!err)
	err = ext2_getblk(np, address / block_size, 1, &buf);
      diskfs_end_catch_exception ();

      rwlock_writer_unlock (&dn->alloc_lock);

      return err;
    }
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
    *size = disk_pager_size;
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
  spin_lock (&node_to_page_lock);
  upi->np->dn->fileinfo = 0;
  spin_unlock (&node_to_page_lock);
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
  disk_pager = malloc (sizeof (struct user_pager_info));
  disk_pager->type = DISK;
  disk_pager->np = 0;
  disk_pager->p = pager_create (disk_pager, MAY_CACHE, MEMORY_OBJECT_COPY_NONE);
  disk_pagerport = pager_get_port (disk_pager->p);
  mach_port_insert_right (mach_task_self (), disk_pagerport, disk_pagerport,
			  MACH_MSG_TYPE_MAKE_SEND);
}  

/* This syncs a single file (NP) to disk.  Wait for all I/O to complete
   if WAIT is set.  NP->lock must be held.  */
void
diskfs_file_update (struct node *np, int wait)
{
  struct user_pager_info *upi;

  spin_lock (&node_to_page_lock);
  upi = np->dn->fileinfo;
  if (upi)
    pager_reference (upi->p);
  spin_unlock (&node_to_page_lock);
  
  if (upi)
    {
      pager_sync (upi->p, wait);
      pager_unreference (upi->p);
    }
  
  pokel_sync (&np->dn->pokel, wait);

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
	  || (S_ISLNK (np->dn_stat.st_mode)));

  spin_lock (&node_to_page_lock);
  if (!np->dn->fileinfo)
    {
      upi = malloc (sizeof (struct user_pager_info));
      upi->type = FILE_DATA;
      upi->np = np;
      diskfs_nref_light (np);
      upi->p = pager_create (upi, MAY_CACHE, MEMORY_OBJECT_COPY_DELAY);
      np->dn->fileinfo = upi;

      spin_lock (&pager_list_lock);
      upi->next = file_pager_list;
      upi->prevp = &file_pager_list;
      if (upi->next)
	upi->next->prevp = &upi->next;
      file_pager_list = upi;
      spin_unlock (&pager_list_lock);
    }
  right = pager_get_port (np->dn->fileinfo->p);
  spin_unlock (&node_to_page_lock);
  
  mach_port_insert_right (mach_task_self (), right, right,
			  MACH_MSG_TYPE_MAKE_SEND);

  return right;
} 

/* Call this when we should turn off caching so that unused memory object
   ports get freed.  */
void
drop_pager_softrefs (struct node *np)
{
  struct user_pager_info *upi;
  
  spin_lock (&node_to_page_lock);
  upi = np->dn->fileinfo;
  if (upi)
    pager_reference (upi->p);
  spin_unlock (&node_to_page_lock);

  if (MAY_CACHE && upi)
    pager_change_attributes (upi->p, 0, MEMORY_OBJECT_COPY_DELAY, 0);
  if (upi)
    pager_unreference (upi->p);
}

/* Call this when we should turn on caching because it's no longer
   important for unused memory object ports to get freed.  */
void
allow_pager_softrefs (struct node *np)
{
  struct user_pager_info *upi;
  
  spin_lock (&node_to_page_lock);
  upi = np->dn->fileinfo;
  if (upi)
    pager_reference (upi->p);
  spin_unlock (&node_to_page_lock);
  
  if (MAY_CACHE && upi)
    pager_change_attributes (upi->p, 1, MEMORY_OBJECT_COPY_DELAY, 0);
  if (upi)
    pager_unreference (upi->p);
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

/* Call function FUNC (which takes one argument, a pager) on each pager, with
   all file pagers being processed before the disk pager.  Make the calls
   while holding no locks. */
static void
pager_traverse (void (*func)(struct user_pager_info *))
{
  struct user_pager_info *p;
  struct item {struct item *next; struct user_pager_info *p;} *list = 0;
  struct item *i;
  
  spin_lock (&pager_list_lock);
  for (p = file_pager_list; p; p = p->next)
    {
      i = alloca (sizeof (struct item));
      i->next = list;
      list = i;
      pager_reference (p->p);
      i->p = p;
    }
  spin_unlock (&pager_list_lock);
  
  for (i = list; i; i = i->next)
    {
      (*func)(i->p);
      pager_unreference (i->p->p);
    }
  
  (*func)(disk_pager);
}

/* Shutdown all the pagers. */
void
diskfs_shutdown_pager ()
{
  void shutdown_one (struct user_pager_info *p)
    {
      pager_shutdown (p->p);
    }

  copy_sblock ();
  write_all_disknodes ();
  pager_traverse (shutdown_one);
}

/* Sync all the pagers. */
void
diskfs_sync_everything (int wait)
{
  void sync_one (struct user_pager_info *p)
    {
      if (p != disk_pager)
	pager_sync (p->p, wait);
      else
	pokel_sync (&sblock_pokel, wait);
    }
  
  copy_sblock ();
  write_all_disknodes ();
  pager_traverse (sync_one);
}
  
