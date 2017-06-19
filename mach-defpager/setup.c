/* Backing store access callbacks for Hurd version of Mach default pager.

   Copyright (C) 2001, 2002, 2007, 2010 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <errno.h>
#include <stddef.h>
#include <assert-backtrace.h>
#include <mach.h>
#include <string.h>
#include <strings.h>

#include "default_pager.h"
#include "kalloc.h"

#include "file_io.h"
#include "default_pager_S.h"

/* This should be in some system header...  XXX  */
int page_aligned (vm_offset_t num)
{
  return trunc_page (num) == num;
}

extern mach_port_t default_pager_default_port; /* default_pager.c */

kern_return_t
S_default_pager_paging_storage (mach_port_t pager,
				mach_port_t device,
				recnum_t *runs, mach_msg_type_number_t nrun,
				default_pager_filename_t name,
				boolean_t add)
{
  struct file_direct *fdp;
  int sizes[DEV_GET_RECORDS_COUNT];
  natural_t count;
  mach_msg_type_number_t i;
  error_t err;
  recnum_t devsize;

  if (pager != default_pager_default_port)
    return KERN_INVALID_ARGUMENT;

  if (! add)
    return remove_paging_file (name); /* XXX ? */

  if (nrun < 2 || nrun % 2 != 0)
    return EINVAL;

  count = DEV_GET_RECORDS_COUNT;
  err = device_get_status (device, DEV_GET_RECORDS, sizes, &count);
  if (err)
    return err;
  if (count < DEV_GET_RECORDS_COUNT || sizes[DEV_GET_RECORDS_RECORD_SIZE] <= 0)
    return EINVAL;
  devsize = sizes[DEV_GET_RECORDS_DEVICE_RECORDS];

  if (vm_page_size % sizes[DEV_GET_RECORDS_RECORD_SIZE] != 0)
    /* We can't write disk blocks larger than pages.  */
    return EINVAL;

  fdp = kalloc (offsetof (struct file_direct, runs[nrun]));
  if (fdp == 0)
    return ENOMEM;

  fdp->device = device;
  fdp->bshift = ffs (sizes[DEV_GET_RECORDS_RECORD_SIZE]) - 1;
  fdp->fd_bsize = sizes[DEV_GET_RECORDS_RECORD_SIZE];
  fdp->nruns = nrun / 2;
  fdp->fd_size = 0;
  for (i = 0; i < nrun; i += 2)
    {
      fdp->runs[i].start = runs[i];
      fdp->runs[i].length = runs[i + 1];
      if (fdp->runs[i].start + fdp->runs[i].length > devsize)
	{
	  kfree (fdp, offsetof (struct file_direct, runs[nrun]));
	  return EINVAL;
	}
      fdp->fd_size += fdp->runs[i].length;
    }

  /* Now really do it.  */
  create_paging_partition (name, fdp, 0, -3);
  return 0;
}


/* Called to read a page from backing store.  */
int
page_read_file_direct (struct file_direct *fdp,
		       vm_offset_t offset,
		       vm_size_t size,
		       vm_offset_t *addr,	/* out */
		       vm_size_t *size_read)	/* out */
{
  struct storage_run *r;
  error_t err;
  char *readloc;
  char *page;
  mach_msg_type_number_t nread;

  assert_backtrace (page_aligned (offset));
  assert_backtrace (size == vm_page_size);

  offset >>= fdp->bshift;

  assert_backtrace (offset + (size >> fdp->bshift) <= fdp->fd_size);

  /* Find the run containing the beginning of the page.  */
  for (r = fdp->runs; offset > r->length; ++r)
    offset -= r->length;

  if (offset + (size >> fdp->bshift) <= r->length)
    /* The first run contains the whole page.  */
    return device_read (fdp->device, 0, r->start + offset,
			size, (char **) addr, size_read);

  /* Read the first part of the run.  */
  err = device_read (fdp->device, 0, r->start + offset,
		     (r->length - offset) << fdp->bshift,
		     (char **) addr, &nread);
  if (err)
    return err;

  size -= nread;
  readloc = (char *) *addr;
  do
    {
      readloc += nread;
      offset += nread >> fdp->bshift;
      if (offset > r->length)
	offset -= r++->length;

      /* We always get another out-of-line page, so we have to copy
	 out of that page and deallocate it.  */
      err = device_read (fdp->device, 0, r->start + offset,
			 (r->length - offset) << fdp->bshift,
			 &page, &nread);
      if (err)
	{
	  vm_deallocate (mach_task_self (),
			 (vm_address_t) *addr, vm_page_size);
	  return err;
	}
      memcpy (readloc, page, nread);
      vm_deallocate (mach_task_self (), (vm_address_t) page, vm_page_size);
      size -= nread;
    } while (size > 0);

  *size_read = vm_page_size;
  return 0;
}

/* Called to write a page to backing store.  */
int
page_write_file_direct(struct file_direct *fdp,
		       vm_offset_t offset,
		       vm_offset_t addr,
		       vm_size_t size,
		       vm_size_t *size_written)	/* out */
{
  struct storage_run *r;
  error_t err;
  int wrote;

  assert_backtrace (page_aligned (offset));
  assert_backtrace (size == vm_page_size);

  offset >>= fdp->bshift;

  assert_backtrace (offset + (size >> fdp->bshift) <= fdp->fd_size);

  /* Find the run containing the beginning of the page.  */
  for (r = fdp->runs; offset > r->length; ++r)
    offset -= r->length;

  if (offset + (size >> fdp->bshift) <= r->length)
    {
      /* The first run contains the whole page.  */
      err = device_write (fdp->device, 0, r->start + offset,
			  (char *) addr, size, &wrote);
      *size_written = wrote;
      return err;
    }

  /* Write the first part of the run.  */
  err = device_write (fdp->device, 0,
		      r->start + offset, (char *) addr,
		      (r->length - offset) << fdp->bshift,
		      &wrote);
  if (err)
    return err;

  size -= wrote;
  do
    {
      mach_msg_type_number_t segsize;

      addr += wrote;
      offset += wrote >> fdp->bshift;
      if (offset > r->length)
	offset -= r++->length;

      segsize = (r->length - offset) << fdp->bshift;
      if (segsize > size)
	segsize = size;
      err = device_write (fdp->device, 0, r->start + offset,
			  (char *) addr, segsize, &wrote);
      if (err)
	{
	  vm_deallocate (mach_task_self (),
			 (vm_address_t) addr, vm_page_size);
	  return err;
	}

      size -= wrote;
    } while (size > 0);

  *size_written = vm_page_size;
  return 0;
}


/* Compatibility entry points used by default_pager_paging_file RPC.  */

kern_return_t
add_paging_file(master_device_port, file_name, linux_signature)
	mach_port_t		master_device_port;
	char			*file_name;
	int			linux_signature;
{
  error_t err;
  mach_port_t dev;
  int sizes[DEV_GET_SIZE_COUNT];
  natural_t count;
  char *devname = file_name;

  assert_backtrace (linux_signature == 0);

  if (!strncmp (file_name, "/dev/", 5))
    devname += 5;

  err = device_open (master_device_port, D_READ|D_WRITE, devname, &dev);
  if (err)
    return err;

  count = DEV_GET_SIZE_COUNT;
  err = device_get_status (dev, DEV_GET_SIZE, sizes, &count);
  if (!err && count < DEV_GET_SIZE_COUNT)
    err = EGRATUITOUS;
  if (err)
    mach_port_deallocate (mach_task_self (), dev);
  else
    {
      struct file_direct *fdp;
      fdp = kalloc (offsetof (struct file_direct, runs[1]));
      if (fdp == 0)
	return ENOMEM;

      fdp->device = dev;
      fdp->fd_bsize = sizes[DEV_GET_SIZE_RECORD_SIZE];
      fdp->bshift = ffs (sizes[DEV_GET_SIZE_RECORD_SIZE]) - 1;
      fdp->fd_size = sizes[DEV_GET_SIZE_DEVICE_SIZE] >> fdp->bshift;
      fdp->nruns = 1;
      fdp->runs[0].start = 0;
      fdp->runs[0].length = fdp->fd_size;

      /* Now really do it.  */
      create_paging_partition (file_name, fdp, 0, 0);
    }

  return err;
}

/*
 * Destroy a paging_partition given a file name
 */
kern_return_t
remove_paging_file (char *file_name)
{
  struct file_direct *fdp = 0;
  kern_return_t kr;

  kr = destroy_paging_partition(file_name, (void **)&fdp);
  if (kr == KERN_SUCCESS && fdp != 0)
    {
      mach_port_deallocate (mach_task_self (), fdp->device);
      kfree (fdp, (char *) &fdp->runs[fdp->nruns] - (char *) fdp);
    }
  return kr;
}
