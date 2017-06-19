/* pager.c - The pager for the display component of a virtual console.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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

#include <errno.h>
#include <assert-backtrace.h>
#include <error.h>
#include <stdio.h>

#include <sys/mman.h>
#include <pthread.h>

#include <hurd.h>
#include <hurd/pager.h>
#include <hurd/console.h>

#include "pager.h"


struct user_pager_info
{
  size_t memobj_npages;
  vm_address_t memobj_pages[0];
};


/* We need a separate bucket for the pager ports.  */
static struct port_bucket *pager_bucket;
static struct pager_requests *pager_requests;


/* Implement the pager_clear_user_data callback from the pager library. */
void
pager_clear_user_data (struct user_pager_info *upi)
{
  size_t idx;

  for (idx = 0; idx < upi->memobj_npages; idx++)
    if (upi->memobj_pages[idx])
      vm_deallocate (mach_task_self (), upi->memobj_pages[idx], vm_page_size);
}


error_t
pager_read_page (struct user_pager_info *upi, vm_offset_t page,
                 vm_address_t *buf, int *writelock)
{
  /* XXX clients should get a read only object.  */
  *writelock = 0;

  if (upi->memobj_pages[page / vm_page_size] != (vm_address_t) NULL)
    {
      *buf = upi->memobj_pages[page / vm_page_size];
      upi->memobj_pages[page / vm_page_size] = (vm_address_t) NULL;
    }
  else
    *buf = (vm_address_t) mmap (0, vm_page_size, PROT_READ|PROT_WRITE,
				MAP_ANON, 0, 0);
  return 0;
}


error_t
pager_write_page (struct user_pager_info *upi, vm_offset_t page,
                  vm_address_t buf)
{
  assert_backtrace (upi->memobj_pages[page / vm_page_size] == (vm_address_t) NULL);
  upi->memobj_pages[page / vm_page_size] = buf;
  return 0;
}


error_t
pager_unlock_page (struct user_pager_info *pager,
                   vm_offset_t address)
{
  assert_backtrace (!"unlocking requested on unlocked page");
  return 0;
}


void
pager_notify_evict (struct user_pager_info *pager,
		    vm_offset_t page)
{
  assert_backtrace (!"unrequested notification on eviction");
}


/* Tell how big the file is. */
error_t
pager_report_extent (struct user_pager_info *upi,
                     vm_address_t *offset,
                     vm_size_t *size)
{
  *offset = 0;
  *size =  upi->memobj_npages * vm_page_size;
  return 0;
}


void
pager_dropweak (struct user_pager_info *upi)
{
}

/* Initialize the pager for the display component.  */
void
user_pager_init (void)
{
  error_t err;

  /* Create the pager bucket, and start to serve paging requests.  */
  pager_bucket = ports_create_bucket ();
  if (! pager_bucket)
    error (5, errno, "Cannot create pager bucket");

  /* Start libpagers worker threads.  */
  err = pager_start_workers (pager_bucket, &pager_requests);
  if (err)
    error (5, err, "Cannot start pager worker threads");
}


/* Create a new pager in USER_PAGER with NPAGES pages, and return a
   mapping to the memory in *USER.  */
error_t
user_pager_create (struct user_pager *user_pager, unsigned int npages,
		   struct cons_display **user)
{
  error_t err;
  struct user_pager_info *upi;

  /* XXX Are the values 1 and MEMORY_OBJECT_COPY_DELAY correct? */
  user_pager->pager = \
    pager_create_alloc (sizeof *upi + sizeof (vm_address_t) * npages,
                        pager_bucket, 1, MEMORY_OBJECT_COPY_DELAY, 0);
  if (!user_pager->pager)
    return errno;

  upi = pager_get_upi (user_pager->pager);
  upi->memobj_npages = npages;
  memset (upi->memobj_pages, 0, sizeof (vm_address_t) * npages);

  user_pager->memobj = pager_get_port (user_pager->pager);
  ports_port_deref (user_pager->pager);

  mach_port_insert_right (mach_task_self (), user_pager->memobj,
			  user_pager->memobj, MACH_MSG_TYPE_MAKE_SEND);

  *user = 0;
  err = vm_map (mach_task_self (),
		(vm_address_t *) user,
		(vm_size_t) npages * vm_page_size,
		(vm_address_t) 0,
		1 /* ! (flags & MAP_FIXED) */,
		user_pager->memobj, 0 /* (vm_offset_t) offset */,
		0 /* ! (flags & MAP_SHARED) */,
                VM_PROT_READ | VM_PROT_WRITE,
                VM_PROT_READ | VM_PROT_WRITE,
                VM_INHERIT_NONE);
  if (err)
    {
      mach_port_deallocate (mach_task_self (), user_pager->memobj);
      return err;
    }

  return 0;
}


/* Destroy the pager USER_PAGER and the mapping at USER.  */
void
user_pager_destroy (struct user_pager *user_pager, struct cons_display *user)
{
  /* The pager will be deallocated by libpager.  */
  vm_deallocate (mach_task_self (), (vm_offset_t) user,
		 pager_get_upi (user_pager->pager)->memobj_npages
		 * vm_page_size);
  mach_port_deallocate (mach_task_self (), user_pager->memobj);
}


/* Allocate a reference for the memory object backing the pager
   USER_PAGER with protection PROT and return it.  */
mach_port_t
user_pager_get_filemap (struct user_pager *user_pager, vm_prot_t prot)
{
  error_t err;

  /* Add a reference for each call, the caller will deallocate it.  */
  err = mach_port_mod_refs (mach_task_self (), user_pager->memobj,
                            MACH_PORT_RIGHT_SEND, +1);
  assert_perror_backtrace (err);

  return user_pager->memobj;
}
