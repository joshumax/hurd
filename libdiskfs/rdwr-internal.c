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

#include "priv.h"
#include <string.h>

/* Actually read or write a file.  The file size must already permit
   the requested access.  NP is the file to read/write.  DATA is a buffer
   to write from or fill on read.  OFFSET is the absolute address (-1
   not permitted here); AMT is the size of the read/write to perform;
   DIR is set for writing and clear for reading.  The inode must
   be locked.  */
error_t
_diskfs_rdwr_internal (struct node *np,
		       char *volatile data,
		       volatile int offset, 
		       volatile int amt, 
		       int dir)
{
  char *window;
  int winoff;
  volatile int cc;
  memory_object_t memobj;
  int err = 0;

  if (dir)
    assert (!diskfs_readonly);

  if (!diskfs_readonly)
    {
      if (dir)
	np->dn_set_mtime = 1;
      else
	np->dn_set_atime = 1;
    }
  
  memobj = diskfs_get_filemap (np);
  mach_port_insert_right (mach_task_self (), memobj, memobj,
			  MACH_MSG_TYPE_MAKE_SEND);
  
  while (amt > 0)
    {
      winoff = trunc_page (offset);

      window = 0;
      /* We map in 8 pages at a time.  Where'd that come from?  Well, the
	 vax has a 1024 pagesize and with 8k blocks that seems like a
	 reasonable number. */
      err = vm_map (mach_task_self (), (u_int *)&window, 8 * __vm_page_size, 
		    0, 1, memobj, winoff, 0, VM_PROT_READ|VM_PROT_WRITE, 
		    VM_PROT_READ|VM_PROT_WRITE, VM_INHERIT_NONE);
      assert (!err);
      diskfs_register_memory_fault_area (diskfs_get_filemap_pager_struct (np), 
					 winoff, window, 8 * __vm_page_size);
      
      if ((offset - winoff) + amt > 8 * __vm_page_size)
	cc = 8 * __vm_page_size - (offset - winoff);
      else
	cc = amt;
      
      if (!(err = diskfs_catch_exception ()))
	{
	  if (dir)
	    bcopy (data, window + (offset - winoff), cc);
	  else
	    bcopy (window + (offset - winoff), data, cc);
	  diskfs_end_catch_exception ();
	}

      vm_deallocate (mach_task_self (), (u_int) window, 8 * __vm_page_size);
      diskfs_unregister_memory_fault_area (window, 8 * __vm_page_size);
      if (err)
	break;
      amt -= cc;
      offset += cc;
      data += cc;
    }
  assert (amt == 0 || err);

  mach_port_deallocate (mach_task_self (), memobj);
  return err;
}
