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

/* Reading and writing of files. this is called by other filesystem
   routines and handles extension of files automatically.  NP is the
   node to be read or written, and must be locked.  DATA will be
   written or filled.  OFF identifies where in thi fel the I/O is to
   take place (-1 is not allowed).  AMT is the size of DATA and tells
   how much to copy.  DIR is 1 for writing and 0 for reading.  CRED is
   the user doing the access (only used to validate attempted file
   extension).  For reads, *AMTREAD is filled with the amount actually
   read.  */
error_t
diskfs_node_rdwr (struct node *np, 
		  char *data,
		  off_t off, 
		  int amt, 
		  int dir,
		  struct protid *cred,
		  int *amtread)
{
  error_t err;
  
  err = ioserver_get_conch (&np->i_conch);
  if (err)
    return err;
  
  if (dir)
    while (off + amt > np->allocsize)
      if (err = diskfs_grow (np, off + amt, cred))
	return err;

  if (off + amt > np->dn_stat.st_size)
    {
      if (dir)
	{
	  np->dn_stat.st_size = off + amt;
	  np->dn_set_ctime = 1;
	}
      else
	amt = np->dn_stat.st_size - off;
    }

  err = _diskfs_rdwr_internal (np, data, off, amt, dir);
  if (!err)
    *amtread = amt;

  return err;
}
