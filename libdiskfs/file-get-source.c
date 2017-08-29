/* file_get_source

   Copyright (C) 2013 Free Software Foundation, Inc.

   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include "priv.h"
#include "fs_S.h"

/* Return information about the source of the receiving
   filesystem.	*/
error_t
diskfs_S_file_get_source (struct protid *cred,
			  char *source)
{
  if (! cred
      || cred->pi.bucket != diskfs_port_bucket
      || cred->pi.class != diskfs_protid_class)
    return EOPNOTSUPP;

  return diskfs_get_source (source, 1024 /* XXX */);
}
