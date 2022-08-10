/* Copyright (C) 2021 Free Software Foundation, Inc.

   Written by Sergey Bugaev <bugaevc@gmail.com>.

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

#include "priv.h"
#include <mach/mach4.h>

mach_port_t
pager_create_ro_port (struct pager *p)
{
  error_t err = 0;;
  mach_port_t port = MACH_PORT_NULL;
  mach_port_t rw_port;
  vm_offset_t offset = 0;
  vm_offset_t start = 0;
  vm_size_t len = ~0;

  rw_port = ports_get_right (p);
  if (!MACH_PORT_VALID (rw_port))
    {
      err = errno;
      goto out;
    }

  err = memory_object_create_proxy (mach_task_self (),
				    VM_PROT_READ | VM_PROT_EXECUTE,
				    &rw_port, MACH_MSG_TYPE_MAKE_SEND, 1,
				    &offset, 1, &start, 1, &len, 1,
				    &port);

 out:
  errno = err;
  return port;
}
