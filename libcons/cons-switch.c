/* cons-switch.c - Switch to another virtual console.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <errno.h>
#include <error.h>
#include <assert.h>

#include "cons.h"

/* Switch the active console in CONS to ID or the current one plus
   DELTA.  This will call back into the user code by doing a
   cons_vcons_activate.  */
error_t
cons_switch (cons_t cons, int id, int delta)
{
  vcons_t vcons = NULL;
  vcons_t active;

  if (!id && !delta)
    return 0;

  mutex_lock (&cons->lock);
  active = cons->active;

  if (!id && !active)
    {
      mutex_unlock (&cons->lock);
      return EINVAL;
    }

  if (id)
    {
      vcons = cons->vcons_list;
      while (vcons && vcons->id != id)
	vcons = vcons->next;
    }
  else if (delta > 0)
    {
      vcons = cons->active;
      while (delta-- > 0)
	{
	  vcons = vcons->next;
	  if (!vcons)
	    vcons = cons->vcons_list;
	}
    }
  else
    {
      assert (delta < 0);
      while (delta++ < 0)
	{
	  vcons = vcons->prev;
	  if (!vcons)
	    vcons = cons->vcons_last;
	}
    }

  if (!vcons)
    {
      mutex_unlock (&cons->lock);
      return ESRCH;
    }

  if (vcons != active)
    {
      error_t err = cons_vcons_activate (vcons);
      if (err)
	{
	  mutex_unlock (&cons->lock);
	  return err;
	}

      cons->active = vcons;
      cons_vcons_refresh (vcons);
    }
  mutex_unlock (&cons->lock);
  return 0;
}
