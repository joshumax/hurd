/* 
   Copyright (C) 1995,2001 Free Software Foundation, Inc.
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
#include <stdlib.h>
#include <errno.h>

struct port_class *
ports_create_class (void (*clean_routine)(void *),
		    void (*dropweak_routine)(void *))
{
  struct port_class *cl;
  
  cl = malloc (sizeof (struct port_class));
  if (! cl)
    {
      errno = ENOMEM;
      return NULL;
    }

  cl->clean_routine = clean_routine;
  cl->dropweak_routine = dropweak_routine;
  cl->flags = 0;
  cl->rpcs = 0;
  cl->count = 0;
  cl->uninhibitable_rpcs = ports_default_uninhibitable_rpcs;

  return cl;
}
