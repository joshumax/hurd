/* Contact parent filesystem and establish ourselves as the translator.

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#include <hurd.h>
#include <hurd/fsys.h>
#include <assert.h>

#include "priv.h"

/* Creates a control port for this filesystem and sends it to BOOTSTRAP with
   fsys_startup.  CONTROL_TYPE is the ports library type for the control
   port, and PROTID_TYPE is the type for ports representing opens of this
   node.  If CONTROL isn't NULL, the trivfs control port is return in it.  If
   any error occurs sending fsys_startup, it is returned, otherwise 0.  */
error_t
trivfs_startup(mach_port_t bootstrap,
	       struct port_class *control_class,
	       struct port_bucket *control_bucket,
	       struct port_class *protid_class,
	       struct port_bucket *protid_bucket,
	       struct trivfs_control **control)
{
  error_t err;
  mach_port_t realnode;
  struct trivfs_control *tcntl;
  mach_port_t mcntl =
    trivfs_handle_port (MACH_PORT_NULL, control_class, control_bucket,
			protid_class, protid_bucket);

  assert(mcntl != MACH_PORT_NULL);

  /* Contact whoever started us.  */
  err = fsys_startup (bootstrap, mcntl, MACH_MSG_TYPE_MAKE_SEND, &realnode);

  /* Install the returned realnode for trivfs's use */
  tcntl = ports_lookup_port (control_bucket, mcntl, control_class);
  assert (tcntl);

  if (!err)
    tcntl->underlying = realnode;

  ports_port_deref (tcntl);

  if (control)
    *control = tcntl;

  return err;
}
