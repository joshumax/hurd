/* libdiskfs implementation of fs.defs:file_chflags
   Copyright (C) 1992, 1993, 1994, 1996, 1998 Free Software Foundation

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

/* Implement file_chflags as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_chflags (struct protid *cred,
		      int flags)
{
#define HI(X)	((X) & 0xffff0000u)
  CHANGE_NODE_FIELD (cred,
		   ({
                     /* Only root is allowed to change the high 16
                        bits.  */
                     if ((HI (flags) != HI (np->dn_stat.st_flags))
                         && ! idvec_contains (cred->user->uids, 0))
                       return EPERM;

		     err = fshelp_isowner (&np->dn_stat, cred->user);
		     if (!err)
		       err = diskfs_validate_flags_change (np, flags);
		     if (!err)
		       {
			 np->dn_stat.st_flags = flags;
			 np->dn_set_ctime = 1;
		       }
		     if (!err && np->filemod_reqs)
		       diskfs_notice_filechange(np, FILE_CHANGED_META, 
						0, 0);
		   }));
#undef HI
}
