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

/* Lookup the virtual console entry with number ID in the console
   CONS, and return it in R_VCONS_ENTRY.  If CREATE is true, the
   virtual console entry will be created if it doesn't exist yet.  If
   CREATE is true, and ID 0, the first free virtual console id is
   used.  CONS must be locked.  */
error_t
cons_lookup (cons_t cons, int id, int create, vcons_list_t *r_vcons_entry)
{
  vcons_list_t previous_vcons_entry = 0;
  vcons_list_t vcons_entry;

  if (!id && !create)
    return EINVAL;

  if (id)
    {
      if (cons->vcons_list && cons->vcons_list->id <= id)
        {
	  previous_vcons_entry = cons->vcons_list;
          while (previous_vcons_entry->next
		 && previous_vcons_entry->next->id <= id)
            previous_vcons_entry = previous_vcons_entry->next;
          if (previous_vcons_entry->id == id)
            {
              *r_vcons_entry = previous_vcons_entry;
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
	  previous_vcons_entry = cons->vcons_list;
          while (previous_vcons_entry && previous_vcons_entry->id == id)
            {
              id++;
              previous_vcons_entry = previous_vcons_entry->next;
            }
        }
    }

  vcons_entry = calloc (1, sizeof (struct vcons_list));
  if (!vcons_entry)
    return ENOMEM;

  vcons_entry->id = id;
  vcons_entry->vcons = NULL;

  /* Insert the virtual console into the doubly linked list.  */
  if (previous_vcons_entry)
    {
      vcons_entry->prev = previous_vcons_entry;
      if (previous_vcons_entry->next)
        {
          previous_vcons_entry->next->prev = vcons_entry;
          vcons_entry->next = previous_vcons_entry->next;
        }
      else
	cons->vcons_last = vcons_entry;
      previous_vcons_entry->next = vcons_entry;
    }
  else
    {
      if (cons->vcons_list)
        {
          cons->vcons_list->prev = vcons_entry;
          vcons_entry->next = cons->vcons_list;
        }
      else
	cons->vcons_last = vcons_entry;
      cons->vcons_list = vcons_entry;
    }

  cons_vcons_add (cons, vcons_entry);
  *r_vcons_entry = vcons_entry;
  return 0;
}
