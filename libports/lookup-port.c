/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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
#include <cthreads.h>
#include <hurd/ihash.h>

void *
ports_lookup_port (struct port_bucket *bucket,
		   mach_port_t port,
		   struct port_class *class)
{
  struct port_info *pi = 0;
  
  mutex_lock (&_ports_lock);

  if (bucket)
    pi = hurd_ihash_find (&bucket->htable, port);
  else
    for (bucket = _ports_all_buckets; bucket; bucket = bucket->next)
      {
	pi = hurd_ihash_find (&bucket->htable, port);
	if (pi)
	  break;
      }
  
  if (pi && class && pi->class != class)
    pi = 0;

  if (pi)
    pi->refcnt++;

  mutex_unlock (&_ports_lock);
  
  return pi;
}

  
