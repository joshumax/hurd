/* Create a new trivfs control port

   Copyright (C) 1996, 1997 Free Software Foundation, Inc.

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
  error_t err;

  /* Perhaps allocate, and perhaps add the specified port classes the ones
     recognized by trivfs.  */
  err = trivfs_add_control_port_class (&control_class);
  if (! err)
    err = trivfs_add_protid_port_class (&protid_class);
  else
    protid_class = 0;

  /* Perhaps allocate new port buckets.  */
  if (! err)
    err = trivfs_add_port_bucket (&control_bucket);
  else
    control_bucket = 0;
  if (! err)
    {
      if (! protid_bucket)
	/* By default, use the same port bucket for both.  */
	protid_bucket = control_bucket;
      err = trivfs_add_port_bucket (&protid_bucket);
    }
  else
    protid_bucket = 0;

  if (! err)
    err = ports_create_port (control_class, control_bucket, 
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
	  goto out;
	}
      
      err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
				&(*control)->file_id);
      if (err)
	{
	  mach_port_destroy (mach_task_self (), (*control)->filesys_id);
	  ports_port_deref (*control);
	  goto out;
	}

      (*control)->hook = 0;
    }

out:
  if (err)
    {
      trivfs_remove_control_port_class (control_class);
      trivfs_remove_protid_port_class (protid_class);
      trivfs_remove_port_bucket (control_bucket);
      trivfs_remove_port_bucket (protid_bucket);
    }

  return err;
}
