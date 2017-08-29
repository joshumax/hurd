/* fsys_get_source

   Copyright (C) 2017 Free Software Foundation, Inc.

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
#include "fsys_S.h"

/* Return information about the source of the translator.  If the
   concept of a source is applicable, SOURCE should refer to the
   source of the translator and should be a description considered
   appropriate in the context of the translator.  For example, if the
   translator is a filesystem residing on a block device, then SOURCE
   should be the file name of the underlying block device.  */
error_t
diskfs_S_fsys_get_source (struct diskfs_control *fsys,
                          mach_port_t reply,
                          mach_msg_type_name_t replytype,
			  char *source)
{
  if (! fsys)
    return EOPNOTSUPP;

  return diskfs_get_source (source, 1024 /* XXX */);
}
