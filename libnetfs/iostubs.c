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

error_t __attribute__((weak))
netfs_S_io_map (struct protid *user, 
		mach_port_t *rdobj, mach_msg_type_name_t *rdobjtype,
		mach_port_t *wrobj, mach_msg_type_name_t *wrobjtype)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_io_map_cntl (struct protid *user,
		     mach_port_t *obj,
		     mach_msg_type_name_t *objtype)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_io_get_conch (struct protid *user)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_io_release_conch (struct protid *user)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_io_eofnotify (struct protid *user)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_io_prenotify (struct protid *user,
		      vm_offset_t start, vm_offset_t stop)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_io_postnotify (struct protid *user,
		       vm_offset_t start, vm_offset_t stop)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_io_readnotify (struct protid *user)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_io_readsleep (struct protid *user)
{
  return EOPNOTSUPP;
}

error_t __attribute__((weak))
netfs_S_io_sigio (struct protid *user)
{
  return EOPNOTSUPP;
}
