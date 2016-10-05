/*
   Copyright (C) 1993,94,95,96,98,99,2001 Free Software Foundation, Inc.

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
#include <errno.h>
#include <sys/stat.h>
#include <hurd/fsys.h>
#include <hurd/fshelp.h>
#include <pthread.h>

struct args
{
  int flags;
};

static error_t
helper (void *cookie, const char *name, mach_port_t control)
{
  struct args *args = cookie;
  error_t err;
  (void) name;
  err = fsys_goaway (control, args->flags);
  if (err == MIG_SERVER_DIED || err == MACH_SEND_INVALID_DEST)
    err = 0;
  return err;
}

/* Shutdown the filesystem; flags are as for fsys_goaway. */
error_t
netfs_shutdown (int flags)
{
  struct args args = { flags };
  int nports;
  int err;

  if ((flags & FSYS_GOAWAY_UNLINK)
      && S_ISDIR (netfs_root_node->nn_stat.st_mode))
    return EBUSY;

  if (flags & FSYS_GOAWAY_RECURSE)
    {
      err = fshelp_map_active_translators (helper, &args);
      if (err)
	return err;
    }

#ifdef NOTYET
  pthread_rwlock_wrlock (&netfs_fsys_lock);
#endif

  /* Permit all current RPC's to finish, and then suspend any new ones.  */
  err = ports_inhibit_class_rpcs (netfs_protid_class);
  if (err)
    {
#ifdef  NOTYET
      pthread_rwlock_unlock (&netfs_fsys_lock);
#endif
      return err;
    }

  nports = ports_count_class (netfs_protid_class);
  if (((flags & FSYS_GOAWAY_FORCE) == 0) && nports)
    /* There are outstanding user ports; resume operations. */
    {
      ports_enable_class (netfs_protid_class);
      ports_resume_class_rpcs (netfs_protid_class);
#ifdef NOTYET
      pthread_rwlock_unlock (&netfs_fsys_lock);
#endif
      return EBUSY;
    }

  if (!(flags & FSYS_GOAWAY_NOSYNC))
    {
      err = netfs_attempt_syncfs (0, flags);
      if (err)
        return err;
    }

  return 0;
}
