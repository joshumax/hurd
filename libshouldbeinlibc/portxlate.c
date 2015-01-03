/* Translate mach port names between two tasks

   Copyright (C) 1996,99,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "portxlate.h"

/* Return a new port name translator translating names between FROM_TASK and
   TO_TASK, in XLATOR, or an error.  */
error_t
port_name_xlator_create (mach_port_t from_task, mach_port_t to_task,
			 struct port_name_xlator **xlator)
{
  error_t err;
  struct port_name_xlator *x = malloc (sizeof (struct port_name_xlator));

  if (! x)
    return ENOMEM;

  mach_port_mod_refs (mach_task_self (), from_task, MACH_PORT_RIGHT_SEND, +1);
  x->from_task = from_task;
  mach_port_mod_refs (mach_task_self (), to_task, MACH_PORT_RIGHT_SEND, +1);
  x->to_task = to_task;
  x->to_names = 0;
  x->to_types = 0;
  x->to_names_len = 0;
  x->to_types_len = 0;

  /* Cache a list of names in TO_TASK.  */
  err = mach_port_names (to_task,
			 &x->to_names, &x->to_names_len,
			 &x->to_types, &x->to_types_len);

  if (! err)
    /* Make an array to hold ports from TO_TASK which have been translated
       into our namespace.  */
    {
      x->ports = malloc (sizeof (mach_port_t) * x->to_names_len);
      if (x->ports)
	{
	  unsigned int i;
	  for (i = 0; i < x->to_names_len; i++)
	    x->ports[i] = MACH_PORT_NULL;
	}
      else
	{
	  munmap ((caddr_t) x->to_names,
		  x->to_names_len * sizeof (mach_port_t));
	  munmap ((caddr_t) x->to_types,
		  x->to_types_len * sizeof (mach_port_type_t));

	  mach_port_deallocate (mach_task_self (), x->to_task);
	  mach_port_deallocate (mach_task_self (), x->from_task);

	  err = ENOMEM;
	}
    }

  if (err)
    free (x);
  else
    *xlator = x;

  return err;
}

/* Free the port name translator X and any resources it holds.  */
void
port_name_xlator_free (struct port_name_xlator *x)
{
  unsigned int i;

  for (i = 0; i < x->to_names_len; i++)
    if (x->ports[i] != MACH_PORT_NULL)
      mach_port_deallocate (mach_task_self (), x->ports[i]);
  free (x->ports);

  munmap ((caddr_t) x->to_names, x->to_names_len * sizeof (mach_port_t));
  munmap ((caddr_t) x->to_types, x->to_types_len * sizeof (mach_port_type_t));

  mach_port_deallocate (mach_task_self (), x->to_task);
  mach_port_deallocate (mach_task_self (), x->from_task);

  free (x);
}

/* Translate the port FROM between the tasks in X, returning the translated
   name in TO, and the types of TO in TO_TYPE, or an error.  If TYPE is
   non-zero, it should be what mach_port_type returns for FROM.  */
error_t
port_name_xlator_xlate (struct port_name_xlator *x,
			mach_port_t from, mach_port_type_t from_type,
			mach_port_t *to, mach_port_type_t *to_type)
{
  error_t err;
  mach_port_t port;
  mach_msg_type_number_t i;
  mach_msg_type_name_t acquired_type;
  mach_msg_type_name_t valid_to_types;

  if (from_type == 0)
    {
      error_t err = mach_port_type (x->from_task, from, &from_type);
      if (err)
	return err;
    }

  if (from_type & MACH_PORT_TYPE_RECEIVE)
    valid_to_types = MACH_PORT_TYPE_SEND;
  else if (from_type & MACH_PORT_TYPE_SEND)
    valid_to_types = MACH_PORT_TYPE_SEND | MACH_PORT_TYPE_RECEIVE;
  else
    return EKERN_INVALID_RIGHT;

  /* Translate the name FROM, in FROM_TASK's namespace into our namespace. */
  err =
    mach_port_extract_right (x->from_task, from,
			     ((from_type & MACH_PORT_TYPE_RECEIVE)
			      ? MACH_MSG_TYPE_MAKE_SEND
			      : MACH_MSG_TYPE_COPY_SEND),
			     &port,
			     &acquired_type);

  if (err)
    return err;

  /* Look for likely candidates in TO_TASK's namespace to test against PORT. */
  for (i = 0; i < x->to_names_len; i++)
    {
      if (x->ports[i] == MACH_PORT_NULL && (x->to_types[i] & valid_to_types))
	/* Port I shows possibilities... */
	{
	  err =
	    mach_port_extract_right (x->to_task,
				     x->to_names[i],
				     ((x->to_types[i] & MACH_PORT_TYPE_RECEIVE)
				      ? MACH_MSG_TYPE_MAKE_SEND
				      : MACH_MSG_TYPE_COPY_SEND),
				     &x->ports[i],
				     &acquired_type);
	  if (err)
	    x->to_types[i] = 0;	/* Don't try to fetch this port again.  */
	}

      if (x->ports[i] == port)
	/* We win!  Port I in TO_TASK is the same as PORT.  */
	break;
  }

  mach_port_deallocate (mach_task_self (), port);

  if (i < x->to_names_len)
    /* Port I is the right translation; return its name in TO_TASK.  */
    {
      *to = x->to_names[i];
      *to_type = x->to_types[i];
      return 0;
    }
  else
    return EKERN_INVALID_NAME;
}
