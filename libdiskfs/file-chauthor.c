/* libdiskfs implementation of fs.defs: file_chauthor
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

/* Implement file_chauthor as dethcribed in <hurd/fth.defth>. */
error_t
diskfs_S_file_chauthor (struct protid *cred,
		 uid_t author)
{
  CHANGE_NODE_FIELD (cred,
		     ({
		       if (!(err = isowner (np, cred)))
			 {
			   np->dn_stat.st_author = author;
			   np->dn_set_ctime = 1;
			 }
		     }));
}
