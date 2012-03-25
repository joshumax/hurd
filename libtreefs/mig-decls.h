/* Type decls for mig-produced server stubs

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "treefs.h"

/* For mig */
typedef struct treefs_handle *treefs_handle_t;

extern treefs_handle_t treefs_begin_using_handle_port(mach_port_t port);
extern void treefs_end_using_handle_port (treefs_handle_t handle);

#if defined(__USE_EXTERN_INLINES) || defined(TREEFS_DEFINE_EI)
TREEFS_EI
treefs_handle_t treefs_begin_using_handle_port(mach_port_t port)
{
  return 
    (struct treefs_handle *)
      ports_lookup_port (0, port, treefs_fsys_port_class);
}

TREEFS_EI void
treefs_end_using_handle_port (treefs_handle_t handle)
{
  if (handle != NULL)
    ports_port_deref (&handle->pi);
}
#endif /* Use extern inlines.  */
