/* Create a new trivfs control port

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include "trivfs.h"

/* Create a new trivfs control port, with underlying node UNDERLYING, and
   return it in CONTROL.  CONTROL_CLASS & CONTROL_BUCKET are passed to
   the ports library to create the control port, and PROTID_CLASS &
   PROTID_BUCKET are used when creating ports representing opens of this
   node.  */
error_t
trivfs_create_control (mach_port_t underlying,
		       struct port_class *control_class,
		       struct port_bucket *control_bucket,
		       struct port_class *protid_class,
		       struct port_bucket *protid_bucket,
		       struct trivfs_control **control)
{
  error_t err =
    ports_create_port (control_class, control_bucket, 
		       sizeof (struct trivfs_control), control);

  if (! err)
    {
      (*control)->underlying = underlying;
      (*control)->protid_class = protid_class;
      (*control)->protid_bucket = protid_bucket;
      err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
				&(*control)->filesys_id);
      if (err)
	{
	  ports_port_deref (*control);
	  return err;
	}
      
      err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
				&(*control)->file_id);
      if (err)
	{
	  mach_port_destroy (mach_task_self (), (*control)->filesys_id);
	  ports_port_deref (*control);
	  return err;
	}

      (*control)->hook = 0;
      mutex_init (&(*control)->lock);
    }

  return err;
}
