/* Store creation from a file name

   Copyright (C) 1996,97,98,2001, 2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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

#include <fcntl.h>
#include <hurd.h>

#include "store.h"

/* Open the file NAME, and return a new store in STORE, which refers to the
   storage underlying it.  CLASSES is used to select classes specified by the
   provider; if it is 0, STORE_STD_CLASSES is used.  FLAGS is set with
   store_set_flags.  A reference to the open file is created (but may be
   destroyed with store_close_source).  */
error_t
store_open (const char *name, int flags,
	    const struct store_class *const *classes,
	    struct store **store)
{
  error_t err;
  int open_flags = (flags & STORE_HARD_READONLY) ? O_RDONLY : O_RDWR;
  file_t node = file_name_lookup (name, open_flags, 0);

  if (node == MACH_PORT_NULL && !(flags & STORE_HARD_READONLY)
      && (errno == EACCES || errno == EROFS))
    {
      flags |= STORE_HARD_READONLY;
      node = file_name_lookup (name, O_RDONLY, 0);
    }

  if (node == MACH_PORT_NULL)
    return errno;

  err = store_create (node, flags, classes, store);
  if (err)
    {
      if (! (flags & STORE_NO_FILEIO))
	/* Try making a store that does file io to NODE.  */
	err = store_file_create (node, flags, store);
      if (err)
	mach_port_deallocate (mach_task_self (), node);
    }

  return err;
}

const struct store_class
store_query_class = { -1, "query", open: store_open };
STORE_STD_CLASS (query);
