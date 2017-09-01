/* Socket I/O operations

   Copyright (C) 2016 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

#include <fcntl.h>

#include "sock.h"
#include "sserver.h"

#include "fs_S.h"

error_t
S_file_check_access (struct sock_user *cred, int *type)
{
  if (!cred)
    return EOPNOTSUPP;

  *type = 0;
  if (cred->sock->read_pipe)
    *type |= O_READ;
  if (cred->sock->write_pipe)
    *type |= O_WRITE;

  return 0;
}
