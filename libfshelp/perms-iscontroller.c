/* see whether a user should be considered a controller of the filesystem
   Copyright (C) 2001, 2008 Free Software Foundation, Inc.
   Written by Neal H Walfield <neal@cs.uml.edu>.

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

#include <unistd.h>
#include "fshelp.h"

/* Check to see whether USER should be considered a controller of the
   filesystem.  Which is to say, check to see if we should give USER the
   control port.  ST is the stat of the root node.  USER is the user
   asking for a send right to the control port.  */
error_t
fshelp_iscontroller (struct stat *st, struct iouser *user)
{
  /* Permitted if USER has the superuser uid, the owner uid or if the
     USER has authority over the process's effective id.  */
  if (idvec_contains (user->uids, 0)
      || idvec_contains (user->uids, st->st_uid)
      || idvec_contains (user->uids, geteuid ()))
    return 0;
  return EPERM;
}
