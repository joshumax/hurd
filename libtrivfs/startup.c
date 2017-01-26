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

/* Start in debug mode, no need to be called by settrans. Common options are
   the same as in trivfs_startup. FILE_NAME is the path of the node where the
   translator is set*/
error_t
trivfs_startup_debug(const char *file_name,
		     struct port_class *control_class,
		     struct port_bucket *control_bucket,
		     struct port_class *protid_class,
		     struct port_bucket *protid_bucket,
		     struct trivfs_control **control)
{
  mach_port_t underlying, right, goaway;
  struct trivfs_control *fsys;
  error_t err =
    trivfs_create_control (MACH_PORT_NULL,
			   control_class, control_bucket,
			   protid_class, protid_bucket,
			   &fsys);

  if (err)
    return err;

  right = ports_get_send_right (fsys);
  goaway = ports_get_send_right (fsys);

  /* Start ourselves as transpator instead of replying to settrans */
  underlying = file_name_lookup(file_name, 0, 0);
  if (underlying == MACH_PORT_NULL)
    err = errno;
  else
    err = file_set_translator(underlying, 0, FS_TRANS_SET, 0, "", 0,
			      right, MACH_MSG_TYPE_COPY_SEND);
  mach_port_deallocate (mach_task_self (), right);

  if (! err)
    fsys->underlying = underlying;

  ports_port_deref (fsys);

  /* Pass back what we got, unless the caller doesn't want it.  */
  if (!err && control)
    *control = fsys;

  /* don't mark us as important and install a SIGTERM handler, so we can be
   * easily killed by Ctrl-C */
  void handler_sigterm(int signum)
  {
    error_t ee;
    ee = fsys_goaway(goaway, 0);
    if (ee == ESUCCESS)
      {
	mach_port_deallocate (mach_task_self (), goaway);
      }
    else if (ee != EBUSY)
      {
	/* Not nice */
	error(99, err, "fsys_goaway");
      }
    /* else the translator is busy, please retry */
  }

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handler_sigterm;
  if (sigaction(SIGTERM, &sa, NULL) < 0)
    err = errno;
  return err;
}
