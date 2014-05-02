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
#include <hurd/ihash.h>

void *
ports_lookup_port (struct port_bucket *bucket,
		   mach_port_t port,
		   struct port_class *class)
{
  struct port_info *pi;

  pthread_rwlock_rdlock (&_ports_htable_lock);

  pi = hurd_ihash_find (&_ports_htable, port);
  if (pi
      && ((class && pi->class != class)
          || (bucket && pi->bucket != bucket)))
    pi = 0;

  if (pi)
    refcounts_unsafe_ref (&pi->refcounts, NULL);

  pthread_rwlock_unlock (&_ports_htable_lock);

  return pi;
}
