/* libdiskfs implementation of fs.defs: file_get_translator
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

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

/* Implement file_get_translator as described in <hurd/fs.defs>. */
error_t
diskfs_S_file_get_translator (struct protid *cred,
			      vm_address_t *trans)
{
  struct node *np;
  error_t error;
  
  if (!cred)
    return EOPNOTSUPP;
  
  np = cred->po->np;

  mutex_lock (&np->lock);
  if (!diskfs_node_translated (np))
    error = EINVAL;
  else
    error = diskfs_get_translator (np, trans);
  mutex_unlock (&np->lock);

  return error;
}
