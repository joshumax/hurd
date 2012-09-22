/* Re-authentication in preparation for an exec

   Copyright (C) 1995, 96, 98, 2000 Free Software Foundation, Inc.

   Stolen by Miles Bader <miles@gnu.ai.mit.edu>, but really
     written by Michael I. Bushnell p/BSG  <mib@gnu.ai.mit.edu>

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

#include <mach.h>
#include <hurd/auth.h>
#include <hurd/io.h>
#include <hurd/process.h>

/* Re-authenticates the ports in PORTS and FDS appropriately (replacing
   PORTS[INIT_PORT_AUTH] with AUTH) for a following exec using the auth port
   AUTH.  Each replaced port has a reference consumed; if an error is
   returned, then PORTS and FDS may contain a mixture of old and new ports,
   however AUTH will only be placed in PORTS upon success.  If SECURE is
   true, then it is assumed the exec will use EXEC_SECURE, and certain ports
   may be replaced by MACH_PORT_NULL, with the expectation that exec will
   fill these in itself; if all ports should be re-authenticated, use 0 for
   this argument, regardless of whether EXEC_SECURE will be used.  If
   MUST_REAUTH is true, then any failure to re-authenticate a port will
   result in the function return the error, otherwise, such failures are
   silently ignored.  */
error_t
exec_reauth (auth_t auth, int secure, int must_reauth,
	     mach_port_t *ports, unsigned num_ports,
	     mach_port_t *fds, unsigned num_fds)
{
  unsigned int i;
  error_t err = 0;

  error_t reauth (mach_port_t *port, int isproc)
    {
      if (*port != MACH_PORT_NULL)
	{
	  mach_port_t newport;
	  mach_port_t ref = mach_reply_port ();
	  error_t err =
	    (isproc ? proc_reauthenticate : io_reauthenticate)
	      (*port, ref, MACH_MSG_TYPE_MAKE_SEND);

	  /* MAKE_SEND is safe here because we destroy REF ourselves. */

	  if (!err)
	    err = auth_user_authenticate (auth, ref, MACH_MSG_TYPE_MAKE_SEND,
					  &newport);
	  mach_port_mod_refs (mach_task_self (), ref, MACH_PORT_RIGHT_RECEIVE, -1);
	  if (err)
	    {
	      if (must_reauth)
		return err;
	      /* Nothing Happens. */
	    }
	  else
	    {
	      if (isproc)
		mach_port_deallocate (mach_task_self (), newport);
	      else
		{
		  mach_port_deallocate (mach_task_self (), *port);
		  *port = newport;
		}
	    }
	}
      return 0;
    }

  /* Re-authenticate all the ports we are handing to the user
     with this new port, and install the new auth port in ports. */
  for (i = 0; i < num_fds && !err; ++i)
    err = reauth (&fds[i], 0);

  if (!err)
    {
      if (secure)
	/* Not worth doing; the exec server will just do it again.  */
	ports[INIT_PORT_CRDIR] = MACH_PORT_NULL;
      else
	err = reauth (&ports[INIT_PORT_CRDIR], 0);
    }
  if (!err)
    err = reauth (&ports[INIT_PORT_PROC], 1);
  if (!err)
    err = reauth (&ports[INIT_PORT_CWDIR], 0);

  if (!err)
    {
      mach_port_deallocate (mach_task_self (), ports[INIT_PORT_AUTH]);
      ports[INIT_PORT_AUTH] = auth;
    }

  return 0;
}
