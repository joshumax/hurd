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
#include <hurd/fsys.h>

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
diskfs_shutdown (int flags)
{
  int nports = -1;
  error_t err;
  struct args args = { flags };

  if ((flags & FSYS_GOAWAY_UNLINK)
       && S_ISDIR (diskfs_root_node->dn_stat.st_mode))
    return EBUSY;

  if (flags & FSYS_GOAWAY_RECURSE)
    {
      err = fshelp_map_active_translators (helper, &args);
      if (err)
	return err;
    }

  pthread_rwlock_wrlock (&diskfs_fsys_lock);

  /* Permit all the current RPC's to finish, and then
     suspend new ones.  */
  err = ports_inhibit_class_rpcs (diskfs_protid_class);
  if (err)
    {
      pthread_rwlock_unlock (&diskfs_fsys_lock);
      return err;
    }

  /* Write everything out and set "clean" state.  Even if we don't in fact
     shut down now, this has the nice effect that a disk that has not been
     written for a long time will not need checking after a crash.  */
  diskfs_sync_everything (1);
  diskfs_set_hypermetadata (1, 1);
  _diskfs_diskdirty = 0;

  /* First, see if there are outstanding user ports. */
  nports = ports_count_class (diskfs_protid_class);
  if (((flags & FSYS_GOAWAY_FORCE) == 0)
      && (nports || diskfs_pager_users ()))
    {
      ports_enable_class (diskfs_protid_class);
      ports_resume_class_rpcs (diskfs_protid_class);
      pthread_rwlock_unlock (&diskfs_fsys_lock);
      return EBUSY;
    }

  if (!diskfs_readonly && (flags & FSYS_GOAWAY_NOSYNC) == 0)
    {
      diskfs_shutdown_pager ();
      diskfs_set_hypermetadata (1, 1);
    }

  return 0;
}
