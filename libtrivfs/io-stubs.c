/*
   Copyright (C) 1993,94,2002 Free Software Foundation, Inc.

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
#include "trivfs_io_S.h"

kern_return_t __attribute__((weak))
trivfs_S_io_map_cntl (struct trivfs_protid *cred,
		      mach_port_t reply,
		      mach_msg_type_name_t replytype,
		      mach_port_t *obj,
		      mach_msg_type_name_t *objtype)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_io_get_conch (struct trivfs_protid *cred,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_io_release_conch (struct trivfs_protid *cred,
			   mach_port_t reply,
			   mach_msg_type_name_t replytype)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_io_eofnotify (struct trivfs_protid *cred,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_io_prenotify (struct trivfs_protid *cred,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       vm_offset_t start,
		       vm_offset_t end)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_io_postnotify (struct trivfs_protid *cred,
			mach_port_t reply,
			mach_msg_type_name_t replytype,
			vm_offset_t start,
			vm_offset_t end)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_io_readsleep (struct trivfs_protid *cred,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_io_sigio (struct trivfs_protid *cred,
		   mach_port_t reply,
		   mach_msg_type_name_t replytype)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
trivfs_S_io_readnotify (struct trivfs_protid *cred,
		     mach_port_t reply,
		     mach_msg_type_name_t replytype)
{
  return EOPNOTSUPP;
}
