/* Iterate a function over the ports in a bucket.
   Copyright (C) 1995, 1999 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "ports.h"
#include <hurd/ihash.h>


/* Internal entrypoint for both ports_bucket_iterate and ports_class_iterate.
   If CLASS is non-null, call FUN only for ports in that class.  */
error_t
_ports_bucket_class_iterate (struct port_bucket *bucket,
			     struct port_class *class,
			     error_t (*fun)(void *))
{
  /* This is obscenely ineffecient.  ihash and ports need to cooperate
     more closely to do it efficiently. */
  void **p;
  size_t i, n, nr_items;
  error_t err;

  pthread_mutex_lock (&_ports_lock);

  if (bucket->htable.nr_items == 0)
    {
      pthread_mutex_unlock (&_ports_lock);
      return 0;
    }

  nr_items = bucket->htable.nr_items;
  p = malloc (nr_items * sizeof *p);
  if (p == NULL)
    {
      pthread_mutex_unlock (&_ports_lock);
      return ENOMEM;
    }

  n = 0;
  HURD_IHASH_ITERATE (&bucket->htable, arg)
    {
      struct port_info *const pi = arg;

      if (class == 0 || pi->class == class)
	{
	  pi->refcnt++;
	  p[n] = pi;
	  n++;
	}
    }
  pthread_mutex_unlock (&_ports_lock);

  if (n != 0 && n != nr_items)
    {
      /* We allocated too much.  Release unused memory.  */
      void **new = realloc (p, n * sizeof *p);
      if (new)
        p = new;
    }

  err = 0;
  for (i = 0; i < n; i++)
    {
      if (!err)
	err = (*fun)(p[i]);
      ports_port_deref (p[i]);
    }

  free (p);
  return err;
}

error_t
ports_bucket_iterate (struct port_bucket *bucket,
		      error_t (*fun)(void *))
{
  return _ports_bucket_class_iterate (bucket, 0, fun);
}
