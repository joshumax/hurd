/* libdiskfs implementetation of fs.defs: file_chown
   Copyright (C) 1992, 1993, 1994, 1996, 1999 Free Software Foundation

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

/* Implement file_chown as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_chown (struct protid *cred,
		     uid_t uid,
		     gid_t gid)
{
  if (uid == (uid_t) -1 && gid == (gid_t) -1) /* No change requested.  */
    return 0;

  CHANGE_NODE_FIELD (cred,
		   ({
		     err = fshelp_isowner (&np->dn_stat, cred->user);
		     if (err
			 || (((uid != (uid_t) -1
			       && !idvec_contains (cred->user->uids, uid))
			      || (gid != (gid_t) -1
				  && !idvec_contains (cred->user->gids, gid)))
			     && !idvec_contains (cred->user->uids, 0)))
		       err = EPERM;
		     else
		       {
			 if (uid != (uid_t) -1)
			   err = diskfs_validate_owner_change (np, uid);
			 if (!err && gid != (gid_t) -1)
			   err = diskfs_validate_group_change (np, gid);
			 if (!err)
			   {
			     if (uid != (uid_t) -1)
			       {
				 np->dn_stat.st_uid = uid;
				 if (np->author_tracks_uid)
				   np->dn_stat.st_author = uid;
			       }
			     if (gid != (gid_t) -1)
			       np->dn_stat.st_gid = gid;
			     np->dn_set_ctime = 1;
			     if (np->filemod_reqs)
			       diskfs_notice_filechange(np,
							FILE_CHANGED_META,
							0, 0);
			   }
		       }
		   }));
}
