/* libdiskfs implementation of fs.defs:file_getcontrol.c
   Copyright (C) 1992, 1993, 1994, 1995, 1996 Free Software Foundation

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

/* Implement file_getcontrol as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_getcontrol (struct protid *cred,
			  mach_port_t *control,
			  mach_msg_type_name_t *controltype)
{
  int error = 0;
  struct port_info *newpi;
  
  if (!cred)
    return EOPNOTSUPP;
  
  if (!idvec_contains (cred->user->uids, 0))
    error = EPERM;
  else
    {
      error = ports_create_port (diskfs_control_class, diskfs_port_bucket,
				 sizeof (struct port_info), &newpi);
      if (! error)
	{
	  spin_lock (&_diskfs_control_lock);
	  _diskfs_ncontrol_ports++;
	  spin_unlock (&_diskfs_control_lock);
	  *control = ports_get_right (newpi);
	  *controltype = MACH_MSG_TYPE_MAKE_SEND;
	  ports_port_deref (newpi);
	}
    }

  return error;
}
