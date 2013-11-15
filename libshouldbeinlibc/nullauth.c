/* Drop all authentication credentials.

   Copyright (C) 2013 Free Software Foundation, Inc.

   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

   This file is part of the GNU Hurd.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <hurd.h>

/* Obtain an empty authentication handle and use it for further
   authentication purposes.  This effectively drops all Unix
   privileges.  */
error_t
setnullauth (void)
{
  error_t err;

  auth_t nullauth;
  err = auth_makeauth (getauth (),
		       NULL, MACH_MSG_TYPE_COPY_SEND, 0,
		       NULL, 0,
		       NULL, 0,
		       NULL, 0,
		       NULL, 0,
		       &nullauth);
  if (err)
    return err;

  err = setauth (nullauth);
  return err;
}
