/* Support for opening `typed' stores

   Copyright (C) 1997 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This task is part of the GNU Hurd.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <string.h>

#include "store.h"

/* Open the store indicated by NAME, which should consist of a store type
   name followed by a ':' and any type-specific name, returning the new store
   in STORE.  CLASSES is used to select classes specified by the type name;
   if it is 0, STORE_STD_CLASSES is used.  */
error_t
store_typed_open (const char *name, int flags,
		  const struct store_class *const *classes,
		  struct store **store)
{
  const struct store_class *const *cl;
  const char *clname_end = strchr (name, ':');

  if (! clname_end)
    clname_end = name + strlen (name);

  if (! classes)
    classes = store_std_classes;
  for (cl = classes; *cl; cl++)
    if (strlen ((*cl)->name) == (clname_end - name)
	&& strncmp (name, (*cl)->name, (clname_end - name)) == 0)
      break;

  if (! *cl)
    return EINVAL;

  if (! (*cl)->open)
    return EOPNOTSUPP;

  if (*clname_end)
    clname_end++;		/* Skip the ':' */

  return (*(*cl)->open) (clname_end, flags, classes, store);
}

struct store_class
store_typed_open_class = { -1, "typed", open: store_typed_open };
