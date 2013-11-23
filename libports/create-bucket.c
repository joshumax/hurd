/* Create a port bucket
   Copyright (C) 1995, 1997, 2001, 2003 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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

#include "ports.h"
#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <hurd/ihash.h>

struct port_bucket *
ports_create_bucket ()
{
  struct port_bucket *ret;
  error_t err;

  ret = malloc (sizeof (struct port_bucket));
  if (! ret)
    {
      errno = ENOMEM;
      return NULL;
    }

  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET, 
			    &ret->portset);
  if (err)
    {
      errno = err;
      free (ret);
      return NULL;
    }

  hurd_ihash_init (&ret->htable, offsetof (struct port_info, hentry));
  ret->rpcs = ret->flags = ret->count = 0;
  _ports_threadpool_init (&ret->threadpool);
  return ret;
}
