/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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

#include "netfs.h"
#include "io_S.h"
#include "modes.h"

error_t
netfs_S_io_set_all_openmodes (struct protid *user, int newbits)
{
  if (!user)
    return EOPNOTSUPP;
  
  pthread_mutex_lock (&user->po->np->lock);
  user->po->openstat &= ~HONORED_STATE_MODES;
  user->po->openstat |= (newbits & HONORED_STATE_MODES);
  pthread_mutex_unlock (&user->po->np->lock);
  return 0;
}
