/*
   Copyright (C) 1995, 1996, 1999 Free Software Foundation, Inc.
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
#include <sys/mman.h>


error_t __attribute__ ((weak))
netfs_file_get_storage_info (struct iouser *cred,
    			     struct node *np,
			     mach_port_t **ports,
			     mach_msg_type_name_t *ports_type,
			     mach_msg_type_number_t *num_ports,
			     int **ints,
			     mach_msg_type_number_t *num_ints,
			     off_t **offsets,
			     mach_msg_type_number_t *num_offsets,
			     char **data,
			     mach_msg_type_number_t *data_len)
{
  *data_len = 0;
  *num_offsets = 0;
  *num_ports = 0;

  if (*num_ints == 0)
    /* Argh */
    {
      *ints = mmap (0, sizeof (int), PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (*ints == (int *) -1)
	return errno;
    }

  *num_ints = 1;
  (*ints)[0] = STORAGE_NETWORK;

  return 0;
}
