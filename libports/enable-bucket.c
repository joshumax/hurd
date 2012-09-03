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

void
ports_enable_bucket (struct port_bucket *bucket)
{
  pthread_mutex_lock (&_ports_lock);
  bucket->flags &= ~PORT_BUCKET_NO_ALLOC;
  if (bucket->flags & PORT_BUCKET_ALLOC_WAIT)
    {
      bucket->flags &= ~PORT_BUCKET_ALLOC_WAIT;
      pthread_cond_broadcast (&_ports_block);
    }
  pthread_mutex_unlock (&_ports_lock);
}
