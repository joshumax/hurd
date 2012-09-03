/* Helper routines for dealing with translators

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

#include "trivfs.h"

/* Return the active translator control port for NODE.  If there is no
   translator, active or passive, MACH_PORT_NULL is returned in CONTROL_PORT.
   If there is a translator, it is started if necessary, and returned in
   CONTROL_PORT.  *DIR_PORT should be a port right to use as the new
   translators parent directory.  If it is MACH_PORT_NULL, a port is created
   from DIR and PARENT_PORT and stored in *DIR_PORT; otherwise DIR and
   PARENT_PORT are not used.  Neither NODE or DIR should be locked when
   calling this function.  */
error_t
treefs_node_get_active_trans (struct treefs_node *node,
			      struct treefs_node *dir,
			      mach_port_t parent_port,
			      mach_port_t *control_port,
			      mach_port_t *dir_port)
{
  /* Fill in dir_port */
  void make_dir_port ()
    {
      pthread_mutex_lock (&dir->lock);
      *dir_port = treefs_node_make_right (dir, 0, parent_port, 0);
      mach_port_insert_right (mach_task_self (),
			      *dir_port, *dir_port, MACH_MSG_TYPE_MAKE_SEND);
      pthread_mutex_unlock (&dir->lock);
    }

  pthread_mutex_lock (&node->active_trans.lock);

  if (node->active_trans.control != MACH_PORT_NULL)
    {
      mach_port_t control = node->active_trans.control;
      mach_port_mod_refs (mach_task_self (), control, 
			  MACH_PORT_RIGHT_SEND, 1);
      pthread_mutex_unlock (&node->active_trans.lock);

      /* Now we have a copy of the translator port that isn't
	 dependent on the translator lock itself.  Relock
	 the directory, make a port from it, and then call
	 fsys_getroot. */

      if (*dir_port == MACH_PORT_NULL)
	make_dir_port ();
	      
      *control_port = control;

      return 0;
    }

  pthread_mutex_unlock (&node->active_trans.lock);

  /* If we get here, then we have no active control port.
     Check to see if there is a passive translator, and if so
     repeat the translator check. */
  pthread_mutex_lock (&node->lock);
  if (!node->istranslated)
    {
      *control_port = MACH_PORT_NULL;
      return 0;
    }

  err = treefs_node_get_translator (node, trans, &trans_len);
  if (err == E2BIG)
    {
      trans = alloca (trans_len);
      err = treefs_node_get_translator (node, trans, &trans_len);
    }
  if (err)
    {
      pthread_mutex_unlock (&node->lock);
      return err;
    }

  if (*dir_port == MACH_PORT_NULL)
    {
      pthread_mutex_unlock (&node->lock);
      make_dir_port ();
      pthread_mutex_lock (&node->lock);
    }

  /* Try starting the translator (this unlocks NODE).  */
  err = treefs_start_translator (node, trans, trans_len, *dir_port);
  if (err)
    return err;

  /* Try again now that we've started the translator...  This call
     should be tail recursive.  */
  return
    treefs_node_get_active_trans (node, dir, parent_port,
				  control_port, dir_port);
}

/* Drop the active translator CONTROL_PORT on NODE, unless it's no longer the
   current active translator, in which case just drop a reference to it.  */
void
treefs_node_drop_active_trans (struct treefs_node *node,
			       mach_port_t control_port)
{
  pthread_mutex_lock (&node->active_trans.lock);
  /* Only zero the control port if it hasn't changed. */
  if (node->active_trans.control == control)
    fshelp_translator_drop (&node->active_trans);
  pthread_mutex_unlock (&node->active_trans.lock);
	      
  /* And we're done with this port. */
  mach_port_deallocate (mach_task_self (), control_port);
}
