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
#include <cthreads.h>
#include <hurd/ihash.h>


/* Internal entrypoint for both ports_bucket_iterate and ports_class_iterate.
   If CLASS is non-null, call FUN only for ports in that class.  */
error_t
_ports_bucket_class_iterate (struct port_bucket *bucket,
			     struct port_class *class,
			     error_t (*fun)(void *))
{
  /* This is obscenely ineffecient.  ihash and ports need to cooperate
     more closely to do it effeciently. */
  struct item
    {
      struct item *next;
      void *p;
    } *list = 0;
  struct item *i, *nxt;
  error_t err;

  mutex_lock (&_ports_lock);
  HURD_IHASH_ITERATE (&bucket->htable, arg)
    {
      struct port_info *const pi = arg;
      struct item *j;

      if (class == 0 || pi->class == class)
	{
	  j = malloc (sizeof (struct item));
	  j->next = list;
	  j->p = pi;
	  list = j;
	  pi->refcnt++;
	}
    }
  mutex_unlock (&_ports_lock);

  err = 0;
  for (i = list; i; i = nxt)
    {
      if (!err)
	err = (*fun)(i->p);
      ports_port_deref (i->p);
      nxt = i->next;
      free (i);
    }
  return err;
}

error_t
ports_bucket_iterate (struct port_bucket *bucket,
		      error_t (*fun)(void *))
{
  return _ports_bucket_class_iterate (bucket, 0, fun);
}
