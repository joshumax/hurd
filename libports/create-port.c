/* Create a new port structure

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

/* Create and return in RESULT a new port in CLASS and BUCKET; SIZE bytes
   will be allocated to hold the port structure and whatever private data the
   user desires.  */
error_t
ports_create_port (struct port_class *class, struct port_bucket *bucket,
		   size_t size, void *result)
{
  return _ports_create_port_internal (class, bucket, size, result, 1);
}
