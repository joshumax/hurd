/* A pager interface for raw mach devices.

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <hurd.h>
#include <hurd/pager.h>
#include <device/device.h>
#include <assert.h>

#include "dev.h"
#include "ptypes.h"

/* ---------------------------------------------------------------- */
/* Pager library callbacks; see <hurd/pager.h> for more info.  */

/* This will be the type used in calls to allocate_port by the pager system.
 */
int pager_port_type = PT_MEMOBJ;

/* For pager PAGER, read one page from offset PAGE.  Set *BUF to be the
   address of the page, and set *WRITE_LOCK if the page must be provided
   read-only.  The only permissable error returns are EIO, EDQUOT, and
   ENOSPC. */
error_t
pager_read_page(struct user_pager_info *upi,
		vm_offset_t page, vm_address_t *buf, int *writelock)
{
  int read;			/* bytes actually read */
  struct dev *dev = (struct dev *)upi;
  error_t err =
    device_read(dev->port,
		0, page / dev->dev_block_size, vm_page_size,
		(io_buf_ptr_t *)buf, &read);
#ifdef MSG
  if (debug)
    {
      mutex_lock(&debug_lock);
      fprintf(debug, "device_read(%d, %d) [pager] => %s, %s, %d\n",
	      page / dev->dev_block_size, vm_page_size,
	      strerror(err), err ? "-" : brep(*buf, read), read);
      mutex_unlock(&debug_lock);
    }
#endif

  *writelock = (dev->flags & DEV_READONLY);

  if (err || read < vm_page_size)
    return EIO;
  else
    return 0;
}

/* For pager PAGER, synchronously write one page from BUF to offset PAGE.  In
   addition, vm_deallocate (or equivalent) BUF.  The only permissable error
   returns are EIO, EDQUOT, and ENOSPC. */
error_t
pager_write_page(struct user_pager_info *upi,
		 vm_offset_t page, vm_address_t buf)
{
  struct dev *dev = (struct dev *)upi;

  if (dev->flags & DEV_READONLY)
    return EROFS;
  else
    {
      int written;
      error_t err =
	device_write(dev->port,
		     0, page / dev->dev_block_size,
		     (io_buf_ptr_t)buf, vm_page_size,
		     &written);
#ifdef MSG
      if (debug)
	{
	  mutex_lock(&debug_lock);
	  fprintf(debug, "device_write(%d, %s, %d) [pager] => %s, %d\n",
		  page / dev->dev_block_size,
		  brep(buf, vm_page_size), vm_page_size,
		  strerror(err), written);
	  mutex_unlock(&debug_lock);
	}
#endif

      vm_deallocate(mach_task_self(), buf, vm_page_size);

      if (err || written < vm_page_size)
	return EIO;
      else
	return 0;
    }
}

/* A page should be made writable. */
error_t
pager_unlock_page(struct user_pager_info *upi, vm_offset_t address)
{
  struct dev *dev = (struct dev *)upi;

  if (dev->flags & DEV_READONLY)
    return EROFS;
  else
    return 0;
}

/* The user must define this function.  It should report back (in
   *OFFSET and *SIZE the minimum valid address the pager will accept
   and the size of the object.   */
error_t
pager_report_extent(struct user_pager_info *upi,
		    vm_address_t *offset, vm_size_t *size)
{
  *offset = 0;
  *size = ((struct dev *)upi)->size;
  return 0;
}

/* This is called when a pager is being deallocated after all extant send
   rights have been destroyed.  */
void
pager_clear_user_data(struct user_pager_info *upi)
{
}
