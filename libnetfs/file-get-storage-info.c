/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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

error_t
netfs_S_file_get_storage_info (struct protid *user,
			       int *class,
			       off_t **addresses,
			       mach_msg_type_number_t *naddresses,
			       size_t *address_units,
			       char *storage_name,
			       mach_port_t *storage_port,
			       mach_msg_type_name_t *storage_port_type,
			       char **storage_data,
			       mach_msg_type_number_t *storage_data_len,
			       int *flags)
{
  if (!user)
    return EOPNOTSUPP;
  
  /* Necessary to keep MiG happy. */
  *naddresses = 0;
  *storage_data_len = 0;
  *storage_port = MACH_PORT_NULL;
  *storage_port_type = MACH_MSG_TYPE_COPY_SEND;
  
  *class = STORAGE_NETWORK;
  return 0;
}

  
