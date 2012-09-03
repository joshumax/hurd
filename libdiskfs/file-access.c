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
#include "fs_S.h"
#include <fcntl.h>

kern_return_t
diskfs_S_file_check_access (struct protid *cred,
			    int *type)
{
  struct node *np;
  
  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;
  pthread_mutex_lock (&np->lock);
  *type = 0;
  if (fshelp_access (&np->dn_stat, S_IREAD, cred->user) == 0)
    *type |= O_READ;
  if (fshelp_access (&np->dn_stat, S_IWRITE, cred->user) == 0)
    *type |= O_WRITE;
  if (fshelp_access (&np->dn_stat, S_IEXEC, cred->user) == 0)
    *type |= O_EXEC;
  
  pthread_mutex_unlock (&np->lock);
  
  return 0;
}
