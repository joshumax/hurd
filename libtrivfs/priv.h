/*
   Copyright (C) 1994, 1997 Free Software Foundation

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

#ifndef TRIVFS_PRIV_H_INCLUDED
#define TRIVFS_PRIV_H_INCLUDED

#include <mach.h>
#include <hurd.h>
#include <hurd/ports.h>
#include <idvec.h>
#include <unistd.h>
#include "trivfs.h"

/* Returns true if UIDS contains either 0 or our user id.  */
static inline int
_is_privileged (struct idvec *uids)
{
  return idvec_contains (uids, 0) || idvec_contains (uids, getuid ());
}

#endif
