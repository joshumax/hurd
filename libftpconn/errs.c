/* Error codes we think may result from file operations we do

   Copyright (C) 1997 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <errno.h>
#include <ftpconn.h>
#include "priv.h"

/* Error codes we think may result from file operations we do.  */
const error_t
ftp_conn_poss_file_errs[] =
{
  EIO, ENOENT, EPERM, EACCES, ENOTDIR, ENAMETOOLONG, ELOOP, EISDIR, EROFS,
  EMFILE, ENFILE, ENXIO, EOPNOTSUPP, ENOSPC, EDQUOT, ETXTBSY, EEXIST,
  0
};
