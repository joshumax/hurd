/* libdiskfs implementation of fs.defs: file_chmod
   Copyright (C) 1992,93,94,96,97,2002 Free Software Foundation, Inc.

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

/* Implement file_chmod as described in <hurd/fs.defs>. */
error_t
diskfs_S_file_chmod (struct protid *cred,
	      mode_t mode)
{
  mode &= ~(S_IFMT | S_ISPARE | S_ITRANS);

  CHANGE_NODE_FIELD (cred,
		     ({
		       if (!(err = fshelp_isowner (&np->dn_stat, cred->user)))
			 {
			   if (!idvec_contains (cred->user->uids, 0))
			     {
			       if (!S_ISDIR (np->dn_stat.st_mode))
				 mode &= ~S_ISVTX;
			       if (!idvec_contains (cred->user->gids,
						    np->dn_stat.st_gid))
				 mode &= ~S_ISGID;
			       if (!idvec_contains (cred->user->uids,
						    np->dn_stat.st_uid))
				 mode &= ~S_ISUID;
			     }
			   mode |= (np->dn_stat.st_mode
				    & (S_IFMT | S_ISPARE | S_ITRANS));
			   err = diskfs_validate_mode_change (np, mode);
			   if (!err)
			     {
			       np->dn_stat.st_mode = mode;
			       np->dn_set_ctime = 1;
			       if (np->filemod_reqs)
				 diskfs_notice_filechange (np,
							   FILE_CHANGED_META,
							   0, 0);
			     }
			 }
		     }));
}
