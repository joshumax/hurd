/*
   Copyright (C) 1993, 1994, 1995 Free Software Foundation

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
#include "fsys_S.h"
#include "fsys_reply_U.h"

/* Implement fsys_goaway as described in <hurd/fsys.defs>. */
error_t
diskfs_S_fsys_goaway (struct diskfs_control *pt,
		      mach_port_t reply,
		      mach_msg_type_name_t reply_type,
		      int flags)
{
  error_t ret;

  if (!pt)
    return EOPNOTSUPP;

  /* XXX FSYS_GOAWAY_NOWAIT not implemented. */

  ret = diskfs_shutdown (flags);

  if (ret == 0)
    {
      /* We are supposed to exit, but first notify the caller. */
      fsys_goaway_reply (reply, reply_type, 0);
      exit (0);
    }

  return ret;
}
