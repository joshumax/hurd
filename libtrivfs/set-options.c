/* Set runtime options

   Copyright (C) 1996 Free Software Foundation

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

#include <hurd/fshelp.h>

#include "priv.h"

/* Set runtime options for FSYS to ARGZ & ARGZ_LEN.  */
error_t
trivfs_set_options (struct trivfs_control *fsys, char *argz, size_t argz_len)
{
  if (trivfs_runtime_argp)
    return fshelp_set_options (trivfs_runtime_argp, 0, argz, argz_len, fsys);
  else
    return EOPNOTSUPP;
}
