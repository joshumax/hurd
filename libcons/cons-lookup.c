/* cons-lookup.c - Looking up virtual consoles.
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
#include <malloc.h>
#include <sys/mman.h>

#include "cons.h"

/* Lookup the virtual console with number ID in the console CONS,
   acquire a reference for it, and return it in R_VCONS.  If CREATE is
   true, the virtual console will be created if it doesn't exist yet.
   If CREATE is true, and ID 0, the first free virtual console id is
   used.  CONS must be locked.  */
error_t
cons_lookup (cons_t cons, int id, int create, vcons_t *r_vcons)
{
  vcons_t previous_vcons = 0;
  vcons_t vcons;

  if (!id && !create)
    return EINVAL;

  if (id)
    {
      if (cons->vcons_list && cons->vcons_list->id <= id)
        {
	  previous_vcons = cons->vcons_list;
          while (previous_vcons->next && previous_vcons->next->id <= id)
            previous_vcons = previous_vcons->next;
          if (previous_vcons->id == id)
            {
              /* previous_vcons->refcnt++; */
              *r_vcons = previous_vcons;
              return 0;
            }
        }
      else if (!create)
	return ESRCH;
    }
  else
    {
      id = 1;
      if (cons->vcons_list && cons->vcons_list->id == 1)
        {
	  previous_vcons = cons->vcons_list;
          while (previous_vcons && previous_vcons->id == id)
            {
              id++;
              previous_vcons = previous_vcons->next;
            }
        }
    }

  vcons = calloc (1, sizeof (struct vcons));
  if (!vcons)
    {
      mutex_unlock (&vcons->cons->lock);
      return ENOMEM;
    }
  vcons->cons = cons;
  /* vcons->refcnt = 1; */
  vcons->id = id;
  mutex_init (&vcons->lock);
  vcons->input = -1;
  vcons->display = MAP_FAILED;
  vcons->notify = NULL;

#if 0
  err = display_create (&vcons->display, cons->encoding ?: DEFAULT_ENCODING,
                        cons->foreground, cons->background);
  if (err)
    {
      free (vcons->name);
      free (vcons);
      return err;
    }

  err = input_create (&vcons->input, cons->encoding ?: DEFAULT_ENCODING);
  if (err)
    {
      display_destroy (vcons->display);
      free (vcons->name);
      free (vcons);
      return err;
    }
#endif

  cons_vcons_add (vcons);

  /* Insert the virtual console into the doubly linked list.  */
  if (previous_vcons)
    {
      vcons->prev = previous_vcons;
      if (previous_vcons->next)
        {
          previous_vcons->next->prev = vcons;
          vcons->next = previous_vcons->next;
        }
      else
	cons->vcons_last = vcons;
      previous_vcons->next = vcons;
    }
  else
    {
      if (cons->vcons_list)
        {
          cons->vcons_list->prev = vcons;
          vcons->next = cons->vcons_list;
        }
      else
	cons->vcons_last = vcons;
      cons->vcons_list = vcons;
    }
  *r_vcons = vcons;
  return 0;
}
