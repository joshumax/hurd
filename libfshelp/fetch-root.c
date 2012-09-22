/*
   Copyright (C) 1995,96,99,2000,02 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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

#include "trans.h"
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <hurd/fsys.h>

error_t
fshelp_fetch_root (struct transbox *box, void *cookie,
		   file_t dotdot,
		   struct iouser *user,
		   int flags,
		   fshelp_fetch_root_callback1_t callback1,
		   fshelp_fetch_root_callback2_t callback2,
		   retry_type *retry, char *retryname,
		   file_t *root)
{
  error_t err;
  mach_port_t control;
  int cancel;
  int i;

 start_over:

  if (box->active != MACH_PORT_NULL)
    assert ((box->flags & TRANSBOX_STARTING) == 0);
  else
    {
      uid_t uid, gid;
      char *argz;
      size_t argz_len;
      error_t err;
      mach_port_t ports[INIT_PORT_MAX];
      int ints[INIT_INT_MAX];
      mach_port_t fds[STDERR_FILENO + 1];
      auth_t ourauth, newauth;

      mach_port_t reauth (mach_port_t port) /* Consumes PORT.  */
	{
	  mach_port_t rend, ret;
	  error_t err;

	  if (port == MACH_PORT_NULL)
	    return port;

	  if (ourauth == MACH_PORT_NULL)
	    /* We have no auth server, so we aren't doing reauthentications.
	       Just pass on our own ports directly.  */
	    return port;

	  rend = mach_reply_port ();

	  /* MAKE_SEND is safe here because we destroy REND ourselves. */
	  err = io_reauthenticate (port, rend,
				   MACH_MSG_TYPE_MAKE_SEND);
	  mach_port_deallocate (mach_task_self (), port);
	  if (! err)
	    err = auth_user_authenticate (newauth, rend,
					  MACH_MSG_TYPE_MAKE_SEND, &ret);
	  if (err)
	    ret = MACH_PORT_NULL;

	  mach_port_mod_refs (mach_task_self (), rend, MACH_PORT_RIGHT_RECEIVE, -1);

	  return ret;
	}
      error_t fetch_underlying (int flags, mach_port_t *underlying,
				mach_msg_type_name_t *underlying_type,
				task_t task, void *cookie)
	{
	  return
	    (*callback2) (box->cookie, cookie, flags,
			  underlying, underlying_type);
	}

      if (box->flags & TRANSBOX_STARTING)
	{
	  box->flags |= TRANSBOX_WANTED;
	  cancel = hurd_condition_wait (&box->wakeup, box->lock);
	  if (cancel)
	    return EINTR;
	  goto start_over;
	}
      box->flags |= TRANSBOX_STARTING;
      mutex_unlock (box->lock);

      err = (*callback1) (box->cookie, cookie, &uid, &gid, &argz, &argz_len);
      if (err)
	goto return_error;

      ourauth = getauth ();
      if (ourauth == MACH_PORT_NULL)
	newauth = ourauth;
      else
	{
	  uid_t uidarray[2] = { uid, uid };
	  gid_t gidarray[2] = { gid, gid };
	  err = auth_makeauth (ourauth, 0, MACH_MSG_TYPE_COPY_SEND, 0,
			       uidarray, 1, uidarray, 2,
			       gidarray, 1, gidarray, 2, &newauth);
	  if (err)
	    goto return_error;
	}

      bzero (ports, INIT_PORT_MAX * sizeof (mach_port_t));
      bzero (fds, (STDERR_FILENO + 1) * sizeof (mach_port_t));
      bzero (ints, INIT_INT_MAX * sizeof (int));

      ports[INIT_PORT_CWDIR] = dotdot;
      ports[INIT_PORT_CRDIR] = reauth (getcrdir ());
      ports[INIT_PORT_AUTH] = newauth;

      fds[STDERR_FILENO] = reauth (getdport (STDERR_FILENO));

      err = fshelp_start_translator_long (fetch_underlying, NULL,
					  argz, argz, argz_len,
					  fds, MACH_MSG_TYPE_COPY_SEND,
					  STDERR_FILENO + 1,
					  ports, MACH_MSG_TYPE_COPY_SEND,
					  INIT_PORT_MAX,
					  ints, INIT_INT_MAX,
					  uid,
					  0, &control);
      for (i = 0; i <= STDERR_FILENO; i++)
	mach_port_deallocate (mach_task_self (), fds[i]);

      for (i = 0; i < INIT_PORT_MAX; i++)
	if (i != INIT_PORT_CWDIR)
	  mach_port_deallocate (mach_task_self (), ports[i]);

      mutex_lock (box->lock);

      free (argz);

    return_error:

      box->flags &= ~TRANSBOX_STARTING;
      if (box->flags & TRANSBOX_WANTED)
	{
	  box->flags &= ~TRANSBOX_WANTED;
	  condition_broadcast (&box->wakeup);
	}

      if (err)
	return err;

      if (! MACH_PORT_VALID (control))
	/* The start translator succeeded, but it returned a bogus port.  */
	return EDIED;

      box->active = control;
    }

  control = box->active;
  mach_port_mod_refs (mach_task_self (), control,
		      MACH_PORT_RIGHT_SEND, 1);
  mutex_unlock (box->lock);

  /* Cancellation point XXX */
  err = fsys_getroot (control, dotdot, MACH_MSG_TYPE_COPY_SEND,
		      user->uids->ids, user->uids->num,
		      user->gids->ids, user->gids->num,
		      flags, retry, retryname, root);

  mutex_lock (box->lock);

  if ((err == MACH_SEND_INVALID_DEST || err == MIG_SERVER_DIED)
      && control == box->active)
    fshelp_set_active (box, MACH_PORT_NULL, 0);
  mach_port_deallocate (mach_task_self (), control);

  if (err == MACH_SEND_INVALID_DEST || err == MIG_SERVER_DIED)
    goto start_over;

  return err;
}
