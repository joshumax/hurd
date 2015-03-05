/* stupid stub functions never called, needed because libdiskfs uses libpager
   Copyright (C) 2001 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */


#include <hurd/pager.h>
#include <stdlib.h>

/* The user must define this function.  For pager PAGER, read one
   page from offset PAGE.  Set *BUF to be the address of the page,
   and set *WRITE_LOCK if the page must be provided read-only.
   The only permissible error returns are EIO, EDQUOT, and ENOSPC. */
error_t
pager_read_page (struct user_pager_info *pager,
		 vm_offset_t page,
		 vm_address_t *buf,
		 int *write_lock)
{
  abort();
  return EIEIO;
}

/* The user must define this function.  For pager PAGER, synchronously
   write one page from BUF to offset PAGE.  In addition, mfree
   (or equivalent) BUF.  The only permissible error returns are EIO,
   EDQUOT, and ENOSPC. */
error_t
pager_write_page (struct user_pager_info *pager,
		  vm_offset_t page,
		  vm_address_t buf)
{
  abort();
  return EIEIO;
}

/* The user must define this function.  A page should be made writable. */
error_t
pager_unlock_page (struct user_pager_info *pager,
		   vm_offset_t address)
{
  abort();
  return EIEIO;
}

void
pager_notify_evict (struct user_pager_info *pager,
		    vm_offset_t page)
{
  abort();
}


/* The user must define this function.  It should report back (in
   *OFFSET and *SIZE the minimum valid address the pager will accept
   and the size of the object.   */
error_t
pager_report_extent (struct user_pager_info *pager,
		     vm_address_t *offset,
		     vm_size_t *size)
{
  abort();
  return EIEIO;
}

/* The user must define this function.  This is called when a pager is
   being deallocated after all extant send rights have been destroyed.  */
void
pager_clear_user_data (struct user_pager_info *pager)
{
  abort();
}

/* The use must define this function.  This will be called when the ports
   library wants to drop weak references.  The pager library creates no
   weak references itself.  If the user doesn't either, then it's OK for
   this function to do nothing.  */
void
pager_dropweak (struct user_pager_info *p)
{
  abort();
}
