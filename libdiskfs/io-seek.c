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

#define diskfs_readonly 0

/* Implement io_seek as described in <hurd/io.defs>. */
error_t
diskfs_S_io_seek (struct protid *cred,
		  off_t offset,
		  int whence,
		  off_t *newoffset)
{
  
  CHANGE_IP_FIELD (cred,
		   ({
		     if (!(err = ioserver_get_conch (&ip->i_conch)))
		       switch (whence)
			 {
			 case SEEK_SET:
			   cred->po->filepointer = offset;
			   break;
			 case SEEK_CUR:
			   cred->po->filepointer += offset;
			   break;
			 case SEEK_END:
			   cred->po->filepointer = ip->di->di_size + offset;
			   break;
			 default:
			   err = EINVAL;
			   break;
			 }
		     *newoffset = cred->po->filepointer;
		   }));
}
