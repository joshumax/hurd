/* libdiskfs implementation of fs.defs: file_utimes
   Copyright (C) 1992, 1993, 1994, 1998, 1999 Free Software Foundation

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

/* Implement file_utimes as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_utimes (struct protid *cred,
		      time_value_t atime,
		      time_value_t mtime)
{
  CHANGE_NODE_FIELD (cred,
		   ({
		     if (!(err = fshelp_isowner (&np->dn_stat, cred->user)))
		       {
			 if (atime.microseconds == -1)
			   np->dn_set_atime = 1;
			 else
			   {
			     np->dn_stat.st_atim.tv_sec = atime.seconds;
			     np->dn_stat.st_atim.tv_nsec = atime.microseconds * 1000;
			     np->dn_set_atime = 0;
			   }
			 
			 if (mtime.microseconds == -1)
			   np->dn_set_mtime = 1;
			 else
			   {
			     np->dn_stat.st_mtim.tv_sec = mtime.seconds;
			     np->dn_stat.st_mtim.tv_nsec = mtime.microseconds * 1000;
			     np->dn_set_mtime = 0;
			   }
			 
			 np->dn_set_ctime = 1;

			 if (np->filemod_reqs)
			   diskfs_notice_filechange (np,
						     FILE_CHANGED_META,
						     0, 0);
		       }
		   }));
}
