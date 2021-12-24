/* Default version of netfs_get_filemap

   Copyright (C) 2021 Free Software Foundation, Inc.

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

/* The user may define this function. Return a memory object proxy port (send
   right) for the file contents of NP. PROT is the maximum allowable
   access. On errors, return MACH_PORT_NULL and set errno.  */
mach_port_t __attribute__ ((weak))
netfs_get_filemap (struct node *np, vm_prot_t prot)
{
  errno = EOPNOTSUPP;
  return MACH_PORT_NULL;
}
