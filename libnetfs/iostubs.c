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

kern_return_t __attribute__((weak))
netfs_S_io_map (struct protid *user,
		mach_port_t *rdobj, mach_msg_type_name_t *rdobjtype,
		mach_port_t *wrobj, mach_msg_type_name_t *wrobjtype)
{
  int flags;
  struct node *node;

  if (!user)
    return EOPNOTSUPP;

  *wrobj = *rdobj = MACH_PORT_NULL;

  node = user->po->np;
  flags = user->po->openstat & (O_READ | O_WRITE);

  pthread_mutex_lock (&node->lock);
  switch (flags)
    {
    case O_READ | O_WRITE:
      *wrobj = *rdobj = netfs_get_filemap (node, VM_PROT_READ |VM_PROT_WRITE);
      if (*wrobj == MACH_PORT_NULL)
	goto error;
      mach_port_mod_refs (mach_task_self (), *rdobj, MACH_PORT_RIGHT_SEND, 1);
      break;
    case O_READ:
      *rdobj = netfs_get_filemap (node, VM_PROT_READ);
      if (*rdobj == MACH_PORT_NULL)
	goto error;
      break;
    case O_WRITE:
      *wrobj = netfs_get_filemap (node, VM_PROT_WRITE);
      if (*wrobj == MACH_PORT_NULL)
	goto error;
      break;
    }
  pthread_mutex_unlock (&node->lock);

  *rdobjtype = MACH_MSG_TYPE_MOVE_SEND;
  *wrobjtype = MACH_MSG_TYPE_MOVE_SEND;

  return 0;

error:
  pthread_mutex_unlock (&node->lock);
  return errno;
}

kern_return_t __attribute__((weak))
netfs_S_io_map_cntl (struct protid *user,
		     mach_port_t *obj,
		     mach_msg_type_name_t *objtype)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
netfs_S_io_get_conch (struct protid *user)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
netfs_S_io_release_conch (struct protid *user)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
netfs_S_io_eofnotify (struct protid *user)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
netfs_S_io_prenotify (struct protid *user,
		      vm_offset_t start, vm_offset_t stop)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
netfs_S_io_postnotify (struct protid *user,
		       vm_offset_t start, vm_offset_t stop)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
netfs_S_io_readnotify (struct protid *user)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
netfs_S_io_readsleep (struct protid *user)
{
  return EOPNOTSUPP;
}

kern_return_t __attribute__((weak))
netfs_S_io_sigio (struct protid *user)
{
  return EOPNOTSUPP;
}
