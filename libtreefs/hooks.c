/* Functions for manipulating hook vectors.

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

#include "treefs.h"

#define HV_SIZE (sizeof (void (*)()) * TREEFS_NUM_HOOKS)

/* Returns a copy of the treefs hook vector HOOKS, or a zero'd vector if HOOKS
   is NULL.  If HOOKS is NULL, treefs_default_hooks is used.  If a memory
   allocation error occurs, NULL is returned.  */
treefs_hook_vector_t
treefs_hooks_clone (treefs_hook_vector_t hooks)
{
  treefs_hook_vector_t clone = malloc (HV_SIZE);
  if (clone != NULL)
    {
      if (hooks == NULL)
	hooks = treefs_default_hooks;
      bcopy (hooks, clone, HV_SIZE);
    }
  return clone;
}

/* Copies each non-NULL entry in OVERRIDES into HOOKS.  */
void
treefs_hooks_override (treefs_hook_vector_t hooks,
		       treefs_hook_vector_t overrides)
{
  int num;
  for (num = 0; num < TREEFS_NUM_HOOKS; num++)
    if (overrides[num] != NULL)
      hooks[num] = overrides[num];
}

/* Sets the hook NUM in HOOKS to HOOK.  */
void
treefs_hooks_set (treefs_hook_vector_t hooks, unsigned num, void (*hook)())
{
  hooks[num] = hook;
}
