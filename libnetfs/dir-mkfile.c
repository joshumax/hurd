/* 
   Copyright (C) 1995,96,97,2001 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "netfs.h"
#include "misc.h"
#include "fs_S.h"

error_t
netfs_S_dir_mkfile (struct protid *diruser, int flags, mode_t mode, 
		    mach_port_t *newfile, mach_msg_type_name_t *newfiletype)
{
  error_t err;
  struct node *np;
  struct iouser *user;
  struct protid *newpi;

  pthread_mutex_lock (&diruser->po->np->lock);
  err = netfs_attempt_mkfile (diruser->user, diruser->po->np, mode, &np);

  if (!err)
    {
      /* the dir is now unlocked and NP is locked */
      flags &= ~OPENONLY_STATE_MODES;
      err = iohelp_dup_iouser (&user, diruser->user);
      if (! err)
        {
          newpi = netfs_make_protid (netfs_make_peropen (np, flags,
							 diruser->po),
				     user);
	  if (newpi)
	    {
	      *newfile = ports_get_right (newpi);
	      *newfiletype = MACH_MSG_TYPE_MAKE_SEND;
	      ports_port_deref (newpi);
	    }
	  else
	    {
	      err = errno;
	      iohelp_free_iouser (user);
	    }
	}
      netfs_nput (np);
    }

  return err;
}
