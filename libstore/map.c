/* Direct store mapping

   Copyright (C) 1997 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>
   This task is part of the GNU Hurd.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include "store.h"

/* Return a memory object paging on STORE.  [among other reasons,] this may
   fail because store contains non-contiguous regions on the underlying
   object.  In such a case you can try calling some of the routines below to
   get a pager.  */
error_t store_map (const struct store *store, vm_prot_t prot, mach_port_t *memobj)
{
  error_t (*map) (const struct store *store, vm_prot_t prot, mach_port_t *memobj) =
    store->class->map;
  return map ? (*map) (store, prot, memobj) : EOPNOTSUPP;
}
