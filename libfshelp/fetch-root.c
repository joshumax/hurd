/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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
		   uid_t *uids, int uids_len,
		   uid_t *gids, int gids_len,
		   int flags, fshelp_callback_t callback,
		   retry_type *retry, char *retryname, 
		   file_t *root)
{
  error_t err;
  mach_port_t control;
  
 start_over:

  if (box->active != MACH_PORT_NULL)
    assert ((box->flags & TRANSBOX_STARTING) == 0);
  else
    {
      mach_port_t underlying;
      uid_t uid, gid;
      char *argz;
      int argz_len;
      error_t err;
      mach_port_t ports[INIT_PORT_MAX];
      int ints[INIT_INT_MAX];
      mach_port_t fds[STDERR_FILENO + 1];
      auth_t ourauth, newauth;
      int uidarray[2], gidarray[2];
      
      mach_port_t
	reauth (mach_port_t port)
	  {
	    mach_port_t rend, ret;
	    error_t err;

	    if (port == MACH_PORT_NULL)
	      return port;

	    rend = mach_reply_port ();
	    err = io_reauthenticate (port, rend, 
				     MACH_MSG_TYPE_MAKE_SEND);
	    assert_perror (err);

	    err = auth_user_authenticate (newauth, port, rend,
					  MACH_MSG_TYPE_MAKE_SEND,
					  &ret);
	    if (err)
	      ret = MACH_PORT_NULL;
	  
	    mach_port_destroy (mach_task_self (), rend);
	    mach_port_deallocate (mach_task_self (), port);
	    return ret;
	  }
      
      if (box->flags & TRANSBOX_STARTING)
	{
	  box->flags |= TRANSBOX_WANTED;
	  condition_wait (&box->wakeup, box->lock);
	  goto start_over;
	}
      box->flags |= TRANSBOX_STARTING;
      mutex_unlock (box->lock);
	  
      err = (*callback) (box->cookie, cookie, &underlying, &uid,
			 &gid, &argz, &argz_len);
      if (err)
	return err;
      
      ourauth = getauth ();
      uidarray[0] = uidarray[1] = uid;
      gidarray[0] = gidarray[1] = gid;
      err = auth_makeauth (ourauth, 0, MACH_MSG_TYPE_MAKE_SEND, 0,
			   uidarray, 1, uidarray, 2,
			   gidarray, 1, gidarray, 2, &newauth);
      assert_perror (err);
      
      bzero (ports, INIT_PORT_MAX * sizeof (mach_port_t));
      bzero (fds, (STDERR_FILENO + 1) * sizeof (mach_port_t));
      bzero (ints, INIT_INT_MAX * sizeof (int));
      
      ports[INIT_PORT_CWDIR] = reauth (getcwdir ());
      ports[INIT_PORT_CRDIR] = reauth (getcrdir ());
      ports[INIT_PORT_AUTH] = newauth;
      
      fds[STDERR_FILENO] = reauth (getdport (STDERR_FILENO));
      
      err = fshelp_start_translator_long (reauth (underlying),
					  MACH_MSG_TYPE_MOVE_SEND,
					  argz, argz, argz_len,
					  fds, MACH_MSG_TYPE_MOVE_SEND,
					  STDERR_FILENO + 1,
					  ports, MACH_MSG_TYPE_MOVE_SEND,
					  INIT_PORT_MAX, 
					  ints, INIT_INT_MAX,
					  0, &control);
      
      mutex_lock (box->lock);
      
      free (argz);
      
      if (err)
	return err;
      
      box->active = control;
      
      box->flags &= ~TRANSBOX_STARTING;
      if (box->flags & TRANSBOX_WANTED)
	{
	  box->flags &= ~TRANSBOX_WANTED;
	  condition_broadcast (&box->wakeup);
	}
    }
  
  control = box->active;
  mach_port_mod_refs (mach_task_self (), control, 
		      MACH_PORT_RIGHT_SEND, 1);
  mutex_unlock (box->lock);
  
  /* Cancellation point XXX */
  err = fsys_getroot (control, dotdot, MACH_MSG_TYPE_COPY_SEND,
		      uids, uids_len, gids, gids_len, flags,
		      retry, retryname, root);
  
  mutex_lock (box->lock);
  
  if ((err == MACH_SEND_INVALID_DEST || err == MIG_SERVER_DIED)
      && control == box->active)
    fshelp_set_active (box, MACH_PORT_NULL, 0);
  mach_port_deallocate (mach_task_self (), control);

  if (err == MACH_SEND_INVALID_DEST || err == MIG_SERVER_DIED)
    goto start_over;
  
  return err;
}

		      
  
