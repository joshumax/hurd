/* 
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

#include "priv.h"
#include "io_S.h"
#include <fcntl.h>

/* Implement io_select as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_select (struct protid *cred,
		    int *type, 
		    int *tag)
{
  if (!cred)
    return EOPNOTSUPP;
  
  mutex_lock (&cred->po->np->lock);
  if (((*type & SELECT_READ) && !(cred->po->openstat & O_READ))
      || ((*type & SELECT_WRITE) && !(cred->po->openstat & O_WRITE)))
    {
      mutex_unlock (&cred->po->np->lock);
      return EBADF;
    }
  mutex_unlock (&cred->po->np->lock);
  /* Select is always possible.  */
  mach_port_deallocate (mach_task_self (), port);
  *type &= ~SELECT_URG;
  return 0;
}
