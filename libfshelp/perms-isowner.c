/* 
   Copyright (C) 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, BSG.

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

#include "fshelp.h"

/* Check to see whether USER should be considered the owner of the
   file identified by ST.  If so, return zero; otherwise return an
   appropriate error code. */
error_t
fshelp_isowner (struct stat *st, struct iouser *user)
{
  /* Permitted if the user has the owner UID, the superuser UID, or if
     the user is in the group of the file and has the group ID as
     their user ID.  */
  if (idvec_contains (user->uids, st->st_uid)
      || idvec_contains (user->uids, 0)
      || (idvec_contains (user->gids, st->st_gid)
	  && idvec_contains (user->uids, st->st_gid)))
    return 0;
  else
    return EPERM;
}
