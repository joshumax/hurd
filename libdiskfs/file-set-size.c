/* libdiskfs implementation of fs.defs: file_set_size
   Copyright (C) 1992, 1993, 1994, 1995 Free Software Foundation

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

/* Implement file_set_size as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_set_size (struct protid *cred,
			off_t size)
{
  CHANGE_NODE_FIELD (cred,
		     ({
		       if (!(cred->po->openstat & O_WRITE) || (size < 0))
			 err = EINVAL;
		       else if (size < np->dn_stat.st_size)
			 {
			   err = diskfs_truncate (np, size);
			   if (!err && np->filemod_reqs)
			     diskfs_notice_filechange (np, 
						       FILE_CHANGED_TRUNCATE, 
						       0, size);
			 }
		       else if (size > np->dn_stat.st_size)
			 {
			   err = diskfs_grow (np, size, cred);
			   if (! err)
			     {
			       np->dn_stat.st_size = size;
			       np->dn_set_ctime = np->dn_set_mtime = 1;
			       if (np->filemod_reqs)
				 diskfs_notice_filechange (np, 
							   FILE_CHANGED_EXTEND,
							   0, size);
			     }
			 }
		       else
			 err = 0; /* Setting to same size.  */
		     }));
}
