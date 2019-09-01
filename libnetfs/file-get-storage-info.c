/*
   Copyright (C) 2001 Free Software Foundation, Inc.
   Written by Neal H Walfield <neal@cs.uml.edu>

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

error_t
netfs_S_file_get_storage_info (struct protid *user,
			       mach_port_t **ports,
			       mach_msg_type_name_t *ports_type,
			       mach_msg_type_number_t *num_ports,
			       int **ints, mach_msg_type_number_t *num_ints,
			       off_t **offsets,
			       mach_msg_type_number_t *num_offsets,
			       data_t *data, mach_msg_type_number_t *data_len)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&user->po->np->lock);
  err = netfs_file_get_storage_info (user->user, user->po->np, ports,
      				     ports_type, num_ports, ints,
				     num_ints, offsets, num_offsets,
				     data, data_len);
  pthread_mutex_unlock (&user->po->np->lock);

  return err;
}
