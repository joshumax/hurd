/* 
   Copyright (C) 1994, 1996 Free Software Foundation

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

/* Implement io_clear_some_openmodes as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_clear_some_openmodes (struct protid *cred,
				  int offbits)
{
  if (!cred)
    return EOPNOTSUPP;
  
  pthread_mutex_lock (&cred->po->np->lock);
  iohelp_get_conch (&cred->po->np->conch);
  cred->po->openstat &= ~(offbits & HONORED_STATE_MODES);
  pthread_mutex_unlock (&cred->po->np->lock);
  return 0;
}
