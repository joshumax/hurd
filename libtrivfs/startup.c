/* Contact parent filesystem and establish ourselves as the translator.

   Copyright (C) 1995, 2000 Free Software Foundation, Inc.

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
#include <assert-backtrace.h>

#include "priv.h"

/* Creates a control port for this filesystem and sends it to BOOTSTRAP with
   fsys_startup.  CONTROL_TYPE is the ports library type for the control
   port, and PROTID_TYPE is the type for ports representing opens of this
   node.  If CONTROL isn't NULL, the trivfs control port is return in it.  If
   any error occurs sending fsys_startup, it is returned, otherwise 0.
   FLAGS specifies how to open the underlying node (O_*).  */
error_t
trivfs_startup(mach_port_t bootstrap, int flags,
	       struct port_class *control_class,
	       struct port_bucket *control_bucket,
	       struct port_class *protid_class,
	       struct port_bucket *protid_bucket,
	       struct trivfs_control **control)
{
  mach_port_t underlying, right;
  struct trivfs_control *fsys;
  error_t err =
    trivfs_create_control (MACH_PORT_NULL,
			   control_class, control_bucket,
			   protid_class, protid_bucket,
			   &fsys);

  if (err)
    return err;

  right = ports_get_send_right (fsys);

  /* Contact whoever started us.  */
  err = fsys_startup (bootstrap, flags, right, MACH_MSG_TYPE_COPY_SEND,
		      &underlying);
  mach_port_deallocate (mach_task_self (), right);

  if (! err)
    fsys->underlying = underlying;

  ports_port_deref (fsys);

  /* Pass back what we got, unless the caller doesn't want it.  */
  if (!err && control)
    *control = fsys;

  /* Mark us as important.  */
  if (! err)
    {
      mach_port_t proc = getproc ();
      if (proc == MACH_PORT_NULL)
	/* /hurd/exec uses libtrivfs.  We have no handle to the proc
	   server in /hurd/exec when it does its handshake with the
	   root filesystem, so fail graciously here.  */
	return 0;

      err = proc_mark_important (proc);
      /* This might fail due to permissions or because the old proc
	 server is still running, ignore any such errors.  */
      if (err == EPERM || err == EMIG_BAD_ID)
	err = 0;

      mach_port_deallocate (mach_task_self (), proc);
    }

  return err;
}
