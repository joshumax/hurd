/* Hack replacement for Linux's kmem_cache_t allocator
   Copyright (C) 2000 Free Software Foundation, Inc.

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

/* Hack replacement for Linux's kmem_cache_t allocator, using plain malloc
   and cthreads locking.  The locking here is probably unnecessary.  */

#include <pthread.h>
#include <linux/malloc.h>

struct kmem_cache_s
{
  pthread_mutex_t lock;

  void *freelist;
  size_t item_size;

  void (*ctor) (void *, kmem_cache_t *, unsigned long);
  void (*dtor) (void *, kmem_cache_t *, unsigned long);
};

kmem_cache_t *
kmem_cache_create (const char *name, size_t item_size,
		   size_t something, unsigned long flags,
		   void (*ctor) (void *, kmem_cache_t *, unsigned long),
		   void (*dtor) (void *, kmem_cache_t *, unsigned long))
{
  kmem_cache_t *new = malloc (sizeof *new);
  if (!new)
    return 0;
  pthread_mutex_init (&new->lock, NULL);
  new->freelist = 0;
  new->item_size = item_size;
  new->ctor = ctor;
  new->dtor = dtor;

  return new;
}


void *
kmem_cache_alloc (kmem_cache_t *cache, int flags)
{
  void *p;

  pthread_mutex_lock (&cache->lock);
  p = cache->freelist;
  if (p != 0) {
    cache->freelist = *(void **)(p + cache->item_size);
    pthread_mutex_unlock (&cache->lock);
    return p;
  }
  pthread_mutex_unlock (&cache->lock);

  p = malloc (cache->item_size + sizeof (void *));
  if (p && cache->ctor)
    (*cache->ctor) (p, cache, flags);
  return p;
}


void
kmem_cache_free (kmem_cache_t *cache, void *p)
{
  void **const nextp = (void **) (p + cache->item_size);

  pthread_mutex_lock (&cache->lock);
  *nextp = cache->freelist;
  cache->freelist = p;
  pthread_mutex_unlock (&cache->lock);

  /* XXX eventually destroy some... */
}
