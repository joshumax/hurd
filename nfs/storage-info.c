/* file_get_storage_info RPC for NFS client filesystem
   Copyright (C) 2001,02 Free Software Foundation, Inc.

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

#include "nfs.h"
#include <hurd/netfs.h>
#include <stdio.h>

error_t
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
  int name_len, fhpos;
  error_t err;

  inline int fmt (size_t buflen)
    {
      return snprintf (*data, buflen,
		       "nfsv%u://%s:%u/%n%*c?rsize=%u&wsize=%u",
		       protocol_version, mounted_hostname, mounted_nfs_port,
		       &fhpos, (int) (np->nn->handle.size * 2),
		       'X', /* filled below */
		       read_size, write_size);
    }

  /* We return the file size, so make sure we have it up to date now.  */
  err = netfs_validate_stat (np, cred);
  if (err)
    return err;

  /* Format the name, and then do it again if the buffer was too short.  */
  name_len = fmt (*data_len);
  if (name_len < 0)
    return errno;
  ++name_len;			/* Include the terminating null.  */
  if (name_len <= *data_len)
    *data_len = name_len;
  else
    {
      *data = mmap (0, name_len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (*data == MAP_FAILED)
	return errno;
      *data_len = fmt (name_len) + 1;
      assert_backtrace (*data_len == name_len);
    }

  /* Now fill in the file handle data in hexadecimal.  */
  {
    static const char hexdigits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < np->nn->handle.size; ++i)
      {
	(*data)[fhpos++] = hexdigits[(uint8_t)np->nn->handle.data[i] >> 4];
	(*data)[fhpos++] = hexdigits[(uint8_t)np->nn->handle.data[i] & 0xf];
      }
  }

  /* Now fill in the rest of the canonical-form storage-info data, which
     just describes a single run of the file's size, a block-size of one
     byte, and our URL as the name for the network store type.  */

  *num_ports = 0;
  *ports_type = MACH_MSG_TYPE_COPY_SEND;

  assert_backtrace (*num_offsets >= 2);	/* mig always gives us some */
  *num_offsets = 2;
  (*offsets)[0] = 0;
  (*offsets)[1] = np->nn_stat.st_size;

  assert_backtrace (*num_ints >= 6);	/* mig always gives us some */
  *num_ints = 1;
  (*ints)[0] = STORAGE_NETWORK;
  (*ints)[1] = 0;		/* XXX readonly if we supported it */
  (*ints)[2] = 1;		/* block size */
  (*ints)[3] = 1;		/* 1 run in offsets list */
  (*ints)[4] = name_len;
  (*ints)[5] = 0;		/* misc len */

  return 0;
}
