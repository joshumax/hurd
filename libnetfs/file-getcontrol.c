/* Return the filesystem corresponding to a file

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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
#include "fsys_S.h"

error_t
netfs_S_file_getcontrol (struct protid *user,
			 mach_port_t *control,
			 mach_msg_type_name_t *controltype)
{
  error_t err;
  struct port_info *pi;
  uid_t *uids, *gids;
  int nuids, ngids;
  int i;

  if (!user)
    return EOPNOTSUPP;

  mutex_lock (&user->po->np->lock);
  netfs_interpret_credential (user->credential, &uids, &nuids, &gids, &ngids);
  mutex_unlock (&user->po->np->lock);
  free (gids);

  for (i = 0; i < nuids; i++)
    if (uids[i] == 0)
      {
	/* They've got root; give it to them. */
	free (uids);
	err = ports_create_port (netfs_port_bucket,
				 sizeof (struct port_info),
				 netfs_control_class, &pi);
	if (err)
	  return err;
	*control = ports_get_right (pi);
	*controltype = MACH_MSG_TYPE_MAKE_SEND;
	ports_port_deref (pi);
	return 0;
      }

  /* Not got root. */
  free (uids);
  return EPERM;
}
