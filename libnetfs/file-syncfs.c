/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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
#include "fs_S.h"
#include <hurd/fsys.h>

error_t
netfs_S_file_syncfs (struct protid *user,
		     int wait,
		     int dochildren)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  if (dochildren)
    {
      char *n = NULL;
      size_t n_len = 0;
      mach_port_t *c;
      size_t c_count, i;

      err = fshelp_get_active_translators (&n, &n_len, &c, &c_count);
      if (err)
	return err;
      free(n);

      for (i = 0; i < c_count; i++)
	fsys_syncfs (c[i], wait, 1);

      free(c);
      if (err)
	return err;
    }


  pthread_mutex_lock (&user->po->np->lock);
  err = netfs_attempt_syncfs (user->user, wait);
  pthread_mutex_unlock (&user->po->np->lock);
  return err;
}
