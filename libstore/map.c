/* Direct store mapping

   Copyright (C) 1997 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <hurd.h>
#include <hurd/io.h>

#include "store.h"

/* Return a memory object paging on STORE.  [among other reasons,] this may
   fail because store contains non-contiguous regions on the underlying
   object.  In such a case you can try calling some of the routines below to
   get a pager.  */
error_t
store_map (const struct store *store, vm_prot_t prot,
	   mach_port_t *memobj)
{
  error_t (*map) (const struct store *store, vm_prot_t prot,
		  mach_port_t *memobj) =
    store->class->map;
  error_t err = map ? (*map) (store, prot, memobj) : EOPNOTSUPP;

  if (err == EOPNOTSUPP && store->source != MACH_PORT_NULL)
    /* Can't map the store directly, but we know it represents the file
       STORE->source, so we can try mapping that instead.  */
    {
      mach_port_t rd_memobj, wr_memobj;
      int ro = (store->flags & STORE_HARD_READONLY);

      if ((prot & VM_PROT_WRITE) && ro)
	return EACCES;

      err = io_map (store->port, &rd_memobj, &wr_memobj);
      if (! err)
	{
	  *memobj = rd_memobj;

	  if (!ro || wr_memobj != MACH_PORT_NULL)
	    /* If either we or the server think this object is writable, then
	       the write-memory-object must be the same as the read one (if
	       we only care about reading, then it can be null too).  */
	    {
	      if (rd_memobj == wr_memobj)
		{
		  if (rd_memobj != MACH_PORT_NULL)
		    mach_port_mod_refs (mach_task_self (), rd_memobj,
					MACH_PORT_RIGHT_SEND, -1);
		}
	      else
		{
		  if (rd_memobj != MACH_PORT_NULL)
		    mach_port_deallocate (mach_task_self (), rd_memobj);
		  if (wr_memobj != MACH_PORT_NULL)
		    mach_port_deallocate (mach_task_self (), wr_memobj);
		  err = EOPNOTSUPP;
		}
	    }
	}
    }

  return err;
}
