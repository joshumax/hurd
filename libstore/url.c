/* Support for opening stores named in URL syntax.

   Copyright (C) 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include "store.h"
#include <string.h>
#include <stdlib.h>

static const struct store_class *
find_url_class (const char *name, const struct store_class *const *classes)
{
  const struct store_class *const *cl;
  const char *clname_end = strchr (name, ':');

  if (clname_end == name || clname_end == 0)
    return 0;

  for (cl = classes ?: store_std_classes; *cl; cl++)
    if (strlen ((*cl)->name) == (clname_end - name)
	&& strncmp (name, (*cl)->name, (clname_end - name)) == 0)
      return *cl;

  return 0;
}

/* Similar to store_typed_open, but NAME must be in URL format,
   i.e. a class name followed by a ':' and any type-specific name.
   Store classes opened this way must strip off the "class:" prefix.
   A leading ':' or no ':' at all is invalid syntax.  */

error_t
store_url_open (const char *name, int flags,
		const struct store_class *const *classes,
		struct store **store)
{
  const struct store_class *cl = find_url_class (name, classes);

  if (cl == 0)
    return EINVAL;

  if (! cl->open)
    /* CL cannot be opened.  */
    return EOPNOTSUPP;

  return (*cl->open) (name, flags, classes, store);
}

static error_t
url_decode (struct store_enc *enc, const struct store_class *const *classes,
	    struct store **store)
{
  const struct store_class *cl;

  /* This is pretty bogus.  We use decode.c's code just to validate
     the generic format and extract the name from the data.  */
  struct store dummy, *dummyptr;
  error_t dummy_create (mach_port_t port, int flags, size_t block_size,
			const struct store_run *runs, size_t num_runs,
			struct store **store)
    {
      *store = &dummy;
      return 0;
    }
  error_t err = store_std_leaf_decode (enc, &dummy_create, &dummyptr);
  if (err)
    return err;

  /* Find the class matching this name.  */
  cl = find_url_class (dummy.name, classes);
  free (dummy.name);
  free (dummy.misc);

  if (cl == 0)
    return EINVAL;

  /* Now that we have the class, we just punt to its own decode hook.  */

  return (!cl->decode ? EOPNOTSUPP : (*cl->decode) (enc, classes, store));
}

/* This class is only trivially different from the "typed" class when used
   by name.  Its real purpose is to decode file_get_storage_info results
   that use the STORAGE_NETWORK type, for which the convention is that the
   name be in URL format (i.e. "type:something").  */

const struct store_class store_url_open_class =
{
  STORAGE_NETWORK, "url",
  open: store_url_open,
  decode: url_decode
};
