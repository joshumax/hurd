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
#include <assert-backtrace.h>
#include <pthread.h>

#include "cons.h"

/* Open the virtual console ID or the virtual console DELTA steps away
   from VCONS in the linked list and return it in R_VCONS, which will
   be locked.  */
error_t
cons_switch (vcons_t vcons, int id, int delta, vcons_t *r_vcons)
{
  error_t err = 0;
  cons_t cons = vcons->cons;
  vcons_list_t vcons_entry = NULL;

  if (!id && !delta)
    return 0;

  pthread_mutex_lock (&cons->lock);
  if (id)
    {
      vcons_entry = cons->vcons_list;
      while (vcons_entry && vcons_entry->id != id)
        vcons_entry = vcons_entry->next;
    }
  else if (delta > 0)
    {
      vcons_entry = vcons->vcons_entry;
      while (delta-- > 0)
        {
          vcons_entry = vcons_entry->next;
          if (!vcons_entry)
            vcons_entry = cons->vcons_list;
        }
    }
  else
    {
      assert_backtrace (delta < 0);
      vcons_entry = vcons->vcons_entry;
      while (delta++ < 0)
        {
          vcons_entry = vcons_entry->prev;
          if (!vcons_entry)
            vcons_entry = cons->vcons_last;
        }
    }

  if (!vcons_entry)
    {
      pthread_mutex_unlock (&cons->lock);
      return ESRCH;
    }

  if (vcons_entry->vcons)
    {
      *r_vcons = vcons_entry->vcons;
      pthread_mutex_lock (&vcons_entry->vcons->lock);
    }
  else
    {
      err = cons_vcons_open (cons, vcons_entry, r_vcons);
      if (!err)
        vcons_entry->vcons = *r_vcons;
    }

  pthread_mutex_unlock (&cons->lock);
  return err;
}
