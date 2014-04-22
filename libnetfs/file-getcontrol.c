/* Return the filesystem corresponding to a file

   Copyright (C) 1995, 1996, 2001 Free Software Foundation, Inc.
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
#include "fs_S.h"
#include <hurd/fshelp.h>

error_t
netfs_S_file_getcontrol (struct protid *user,
			 mach_port_t *control,
			 mach_msg_type_name_t *controltype)
{
  error_t err;
  struct port_info *pi;

  if (!user)
    return EOPNOTSUPP;

  err = fshelp_iscontroller (&netfs_root_node->nn_stat, user->user);
  if (err)
    return err;
  
  /* They've have the appropriate credentials; give it to them. */
  err = ports_create_port (netfs_control_class, netfs_port_bucket,
			   sizeof (struct port_info), &pi);
  if (err)
    return err;

  *control = ports_get_right (pi);
  *controltype = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (pi);
  return 0;
}
