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

/* Implement io_stat as described in <hurd/io.defs>. */
error_t
diskfs_S_io_stat (struct protid *cred,
		  io_statbuf_t *statbuf)
{
  struct node *np;
  error_t error;

  if (!cred)
    return EOPNOTSUPP;
  
  np = cred->po->np;
  mutex_lock (&ip->lock);
  error = ioserver_get_conch (&ip->i_conch);
  if (!error)
    bcopy (np->dn_stat, statbuf, sizeof (struct stat));

 out:
  mutex_unlock (&ip->i_toplock);
  return error;
}
