/*
   Copyright (C) 1994 Free Software Foundation

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
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include "io_S.h"

diskfs_S_io_server_version (struct protid *cred,
			    char *server_name,
			    int *major,
			    int *minor,
			    int *edit)
{
  if (cred)
    {
      strcpy (server_name, diskfs_server_name);
      *major = diskfs_major_version;
      *minor = diskfs_minor_version;
      *edit = diskfs_edit_version;
      return 0;
    }
  else
    return EOPNOTSUPP;
}
