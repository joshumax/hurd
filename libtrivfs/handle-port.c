/*
   Copyright (C) 1994, 1995 Free Software Foundation

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

/* Backwards compatibility.  Use trivfs_create_control.  */
mach_port_t
trivfs_handle_port (mach_port_t realnode,
		    struct port_class *control_class,
		    struct port_bucket *control_bucket,
		    struct port_class *protid_class,
		    struct port_bucket *protid_bucket)
{
  mach_port_t right;
  struct trivfs_control *control;
  error_t err =
    trivfs_create_control (realnode,
			   control_class, control_bucket,
			   protid_class, protid_bucket,
			   &control);

  if (err)
    return MACH_PORT_NULL;

  right = ports_get_right (control);
  ports_port_deref (control);

  return right;
}

#include "linkwarn.h"
obslete (trivfs_handle_port, trivfs_create_control)
