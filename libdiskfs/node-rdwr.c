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
   routines and handles extension of files and reads of more than is
   left automatically.  NP is the node to be read or written.  DATA
   will be written or filled.  OFF identifies where in thi fel the I/O
   is to take place (-1 is not allowed).  AMT is the size of DATA and
   tells how much to copy.  DIR is 1 for writing and 0 for reading.
   CRED is the user doing the access (only used to validate attempted
   file extension). */
error_t
diskfs_noderdwr (struct inode *ip, 
	 char *data,
	 off_t off, 
	 int amt, 
	 int dir,
	 struct protid *cred)
{
  error_t err;
  
  if (err = ioserver_get_conch (&ip->i_conch))
    return err;
  
  if (!(err = catch_exception ()))
    {
      if (dir)
	while (off + amt > ip->i_allocsize)
	  if (err = file_extend (ip, off + amt, cred))
	    return err;

      if (off + amt > ip->di->di_size)
	{
	  assert (dir);		/* reads can't go past EOF internally */
	  if (dir)
	    {
	      ip->di->di_size = off + amt;
	      ip->di->di_ctime = wallclock->seconds;
	    }
	}
      end_catch_exception ();
    }
  if (err)
    return err;
  
  err = io_rdwr (ip, data, off, amt, dir);

  return err;
}
