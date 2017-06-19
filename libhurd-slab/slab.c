/* Copyright (C) 2003, 2005 Free Software Foundation, Inc.
   Written by Johan Rydberg.

   This file is part of the GNU Hurd.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this program; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <assert-backtrace.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

#include "slab.h"

#define SLAB_PAGES 4


/* Number of pages the slab allocator has allocated.  */
static int __hurd_slab_nr_pages;


/* Buffer control structure.  Lives at the end of an object.  If the
   buffer is allocated, SLAB points to the slab to which it belongs.
   If the buffer is free, NEXT points to next buffer on free list.  */
union hurd_bufctl
{
  union hurd_bufctl *next;
  struct hurd_slab *slab;
};


/* When the allocator needs to grow a cache, it allocates a slab.  A
   slab consists of one or more pages of memory, split up into equally
   sized chunks.  */
struct hurd_slab
{
  struct hurd_slab *next;
  struct hurd_slab *prev;

  /* The reference counter holds the number of allocated chunks in
     the slab.  When the counter is zero, all chunks are free and
     the slab can be relinquished.  */
  int refcount;

  /* Single linked list of free buffers in the slab.  */
  union hurd_bufctl *free_list;
};

/* Allocate a buffer in *PTR of size SIZE which must be a power of 2
   and self aligned (i.e. aligned on a SIZE byte boundary) for slab
   space SPACE.  Return 0 on success, an error code on failure.  */
static error_t
allocate_buffer (struct hurd_slab_space *space, size_t size, void **ptr)
{
  if (space->allocate_buffer)
    return space->allocate_buffer (space->hook, size, ptr);
  else
    {
      *ptr = mmap (NULL, size, PROT_READ|PROT_WRITE,
		   MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
      if (*ptr == MAP_FAILED)
	return errno;
      else
	return 0;
    }
}

/* Deallocate buffer BUFFER of size SIZE which was allocated for slab
   space SPACE.  Return 0 on success, an error code on failure.  */
static error_t
deallocate_buffer (struct hurd_slab_space *space, void *buffer, size_t size)
{
  if (space->deallocate_buffer)
    return space->deallocate_buffer (space->hook, buffer, size);
  else
    {
      if (munmap (buffer, size) == -1)
	return errno;
      else
	return 0;
    }
}

/* Insert SLAB into the list of slabs in SPACE.  SLAB is expected to
   be complete (so it will be inserted at the end).  */
static void
insert_slab (struct hurd_slab_space *space, struct hurd_slab *slab)
{
  assert_backtrace (slab->refcount == 0);
  if (space->slab_first == 0)
    space->slab_first = space->slab_last = slab;
  else
    {
      space->slab_last->next = slab;
      slab->prev = space->slab_last;
      space->slab_last = slab;
    }
}


/* Remove SLAB from list of slabs in SPACE.  */
static void
remove_slab (struct hurd_slab_space *space, struct hurd_slab *slab)
{
  if (slab != space->slab_first
      && slab != space->slab_last)
    {
      slab->next->prev = slab->prev;
      slab->prev->next = slab->next;
      return;
    }
  if (slab == space->slab_first)
    {
      space->slab_first = slab->next;
      if (space->slab_first)
	space->slab_first->prev = NULL;
    }
  if (slab == space->slab_last)
    {
      if (slab->prev)
	slab->prev->next = NULL;
      space->slab_last = slab->prev;
    }
}


/* Iterate through slabs in SPACE and release memory for slabs that
   are complete (no allocated buffers).  */
static error_t
reap (struct hurd_slab_space *space)
{
  struct hurd_slab *s, *next, *new_first;
  error_t err = 0;

  for (s = space->slab_first; s; s = next)
    {
      next = s->next;

      /* If the reference counter is zero there is no allocated
	 buffers, so it can be freed.  */
      if (!s->refcount)
	{
	  remove_slab (space, s);
	  
	  /* If there is a destructor it must be invoked for every 
	     buffer in the slab.  */
	  if (space->destructor)
	    {
	      union hurd_bufctl *bufctl;
	      for (bufctl = s->free_list; bufctl; bufctl = bufctl->next)
		{
		  void *buffer = (((void *) bufctl) 
				  - (space->size - sizeof *bufctl));
		  (*space->destructor) (space->hook, buffer);
		}
	    }
	  /* The slab is located at the end of the page (with the buffers
	     in front of it), get address by masking with page size.  
	     This frees the slab and all its buffers, since they live on
	     the same page.  */
	  err = deallocate_buffer (space, (void *) (((uintptr_t) s)
						    + sizeof (struct hurd_slab)
						    - space->slab_size), 
				   space->slab_size);
	  if (err)
	    break;
	  __hurd_slab_nr_pages--;
	}
    }

  /* Even in the case of an error, first_free must be updated since
     that slab may have been deallocated.  */
  new_first = space->slab_first;
  while (new_first)
    {
      if (new_first->refcount != space->full_refcount)
	break;
      new_first = new_first->next;
    }
  space->first_free = new_first;

  return err;
}


/* Initialize slab space SPACE.  */
static void
init_space (hurd_slab_space_t space)
{
  size_t size = space->requested_size + sizeof (union hurd_bufctl);
  size_t alignment = space->requested_align;

  /* If SIZE is so big that one object can not fit into a page
     something gotta be really wrong.  */ 
  size = (size + alignment - 1) & ~(alignment - 1);
  assert_backtrace (size <= (space->slab_size 
		   - sizeof (struct hurd_slab) 
		   - sizeof (union hurd_bufctl)));

  space->size = size;

  /* Number of objects that fit into one page.  Used to detect when
     there are no free objects left in a slab.  */
  space->full_refcount 
    = ((space->slab_size - sizeof (struct hurd_slab)) / size);

  /* FIXME: Notify pager's reap functionality about this slab
     space.  */

  space->initialized = true;
}


/* SPACE has no more memory.  Allocate new slab and insert it into the
   list, repoint free_list and return possible error.  */
static error_t
grow (struct hurd_slab_space *space)
{
  error_t err;
  struct hurd_slab *new_slab;
  union hurd_bufctl *bufctl;
  int nr_objs, i;
  void *p;

  /* If the space has not yet been initialized this is the place to do
     so.  It is okay to test some fields such as first_free prior to
     initialization since they will be a null pointer in any case.  */
  if (!space->initialized)
    init_space (space);

  err = allocate_buffer (space, space->slab_size, &p);
  if (err)
    return err;

  __hurd_slab_nr_pages++;

  new_slab = (p + space->slab_size - sizeof (struct hurd_slab));
  memset (new_slab, 0, sizeof (*new_slab));

  /* Calculate the number of objects that the page can hold.
     SPACE->size should be adjusted to handle alignment.  */
  nr_objs = ((space->slab_size - sizeof (struct hurd_slab))
	     / space->size);
  
  for (i = 0; i < nr_objs; i++, p += space->size)
    {
      /* Invoke constructor at object creation time, not when it is
	 really allocated (for faster allocation).  */
      if (space->constructor)
	{
	  error_t err = (*space->constructor) (space->hook, p);
	  if (err)
	    {
	      /* The allocated page holds both slab and memory
		 objects.  Call the destructor for objects that has
		 been initialized.  */
	      for (bufctl = new_slab->free_list; bufctl;
		   bufctl = bufctl->next)
		{
		  void *buffer = (((void *) bufctl) 
				  - (space->size - sizeof *bufctl));
		  (*space->destructor) (space->hook, buffer);
		}

	      deallocate_buffer (space, p, space->slab_size);
	      return err;
	    }
	}

      /* The most activity is in front of the object, so it is most
	 likely to be overwritten if a freed buffer gets accessed.
	 Therefor, put the bufctl structure at the end of the
	 object.  */
      bufctl = (p + space->size - sizeof *bufctl);
      bufctl->next = new_slab->free_list;
      new_slab->free_list = bufctl;
    }

  /* Insert slab into the list of available slabs for this cache.  The
     only time a slab should be allocated is when there is no more
     buffers, so it is safe to repoint first_free.  */  
  insert_slab (space, new_slab);
  space->first_free = new_slab;
  return 0;
}


/* Initialize the slab space SPACE.  */
error_t
hurd_slab_init (hurd_slab_space_t space, size_t size, size_t alignment,
		hurd_slab_allocate_buffer_t allocate_buffer,
		hurd_slab_deallocate_buffer_t deallocate_buffer,
		hurd_slab_constructor_t constructor,
		hurd_slab_destructor_t destructor,
		void *hook)
{
  error_t err;

  /* Initialize all members to zero by default.  */
  memset (space, 0, sizeof (struct hurd_slab_space));

  if (!alignment)
    /* FIXME: Is this a good default?  Maybe eight (8) is better,
       since several architectures require that double and friends are
       eight byte aligned.  */
    alignment = __alignof__ (void *);

  space->requested_size = size;
  space->requested_align = alignment;
  space->slab_size = getpagesize () * SLAB_PAGES;

  /* Testing the size here avoids an assertion in init_space.  */
  size = size + sizeof (union hurd_bufctl);
  size = (size + alignment - 1) & ~(alignment - 1);
  if (size > (space->slab_size - sizeof (struct hurd_slab)
	      - sizeof (union hurd_bufctl)))
    return EINVAL;

  err = pthread_mutex_init (&space->lock, NULL);
  if (err)
    return err;

  space->allocate_buffer = allocate_buffer;
  space->deallocate_buffer = deallocate_buffer;
  space->constructor = constructor;
  space->destructor = destructor;
  space->hook = hook;

  /* The remaining fields will be initialized by init_space.  */
  return 0;
}


/* Create a new slab space with the given object size, alignment,
   constructor and destructor.  ALIGNMENT can be zero.  */
error_t
hurd_slab_create (size_t size, size_t alignment,
		  hurd_slab_allocate_buffer_t allocate_buffer,
		  hurd_slab_deallocate_buffer_t deallocate_buffer,
		  hurd_slab_constructor_t constructor,
		  hurd_slab_destructor_t destructor,
		  void *hook,
		  hurd_slab_space_t *r_space)
{
  hurd_slab_space_t space;
  error_t err;

  space = malloc (sizeof (struct hurd_slab_space));
  if (!space)
    return ENOMEM;

  err = hurd_slab_init (space, size, alignment,
			allocate_buffer, deallocate_buffer,
			constructor, destructor, hook);
  if (err)
    {
      free (space);
      return err;
    }

  *r_space = space;
  return 0;
}


/* Destroy all objects and the slab space SPACE.  Returns EBUSY if
   there are still allocated objects in the slab.  */
error_t
hurd_slab_destroy (hurd_slab_space_t space)
{
  error_t err;

  /* The caller wants to destroy the slab.  It can not be destroyed if
     there are any outstanding memory allocations.  */
  pthread_mutex_lock (&space->lock);
  err = reap (space);
  if (err)
    {
      pthread_mutex_unlock (&space->lock);
      return err;
    }

  if (space->slab_first)
    {
      /* There are still slabs, i.e. there is outstanding allocations.
	 Return EBUSY.  */
      pthread_mutex_unlock (&space->lock);
      return EBUSY;
    }

  /* FIXME: Remove slab space from pager's reap functionality.  */

  return 0;
}


/* Destroy all objects and the slab space SPACE.  If there were no
   outstanding allocations free the slab space.  Returns EBUSY if
   there are still allocated objects in the slab space.  */
error_t
hurd_slab_free (hurd_slab_space_t space)
{
  error_t err = hurd_slab_destroy (space);
  if (err)
    return err;
  free (space);
  return 0;
}


/* Allocate a new object from the slab space SPACE.  */
error_t
hurd_slab_alloc (hurd_slab_space_t space, void **buffer)
{
  error_t err;
  union hurd_bufctl *bufctl;

  pthread_mutex_lock (&space->lock);

  /* If there is no slabs with free buffer, the cache has to be
     expanded with another slab.  If the slab space has not yet been
     initialized this is always true.  */
  if (!space->first_free)
    {
      err = grow (space);
      if (err)
	{
	  pthread_mutex_unlock (&space->lock);
	  return err;
	}
    }

  /* Remove buffer from the free list and update the reference
     counter.  If the reference counter will hit the top, it is
     handled at the time of the next allocation.  */
  bufctl = space->first_free->free_list;
  space->first_free->free_list = bufctl->next;
  space->first_free->refcount++;
  bufctl->slab = space->first_free;

  /* If the reference counter hits the top it means that there has
     been an allocation boost, otherwise dealloc would have updated
     the first_free pointer.  Find a slab with free objects.  */
  if (space->first_free->refcount == space->full_refcount)
    {
      struct hurd_slab *new_first = space->slab_first;
      while (new_first)
	{
	  if (new_first->refcount != space->full_refcount)
	    break;
	  new_first = new_first->next;
	}
      /* If first_free is set to NULL here it means that there are
	 only empty slabs.  The next call to alloc will allocate a new
	 slab if there was no call to dealloc in the meantime.  */
      space->first_free = new_first;
    }
  *buffer = ((void *) bufctl) - (space->size - sizeof *bufctl);
  pthread_mutex_unlock (&space->lock);
  return 0;
}


static inline void
put_on_slab_list (struct hurd_slab *slab, union hurd_bufctl *bufctl)
{
  bufctl->next = slab->free_list;
  slab->free_list = bufctl;
  slab->refcount--;
  assert_backtrace (slab->refcount >= 0);
}


/* Deallocate the object BUFFER from the slab space SPACE.  */
void
hurd_slab_dealloc (hurd_slab_space_t space, void *buffer)
{
  struct hurd_slab *slab;
  union hurd_bufctl *bufctl;

  assert_backtrace (space->initialized);

  pthread_mutex_lock (&space->lock);

  bufctl = (buffer + (space->size - sizeof *bufctl));
  put_on_slab_list (slab = bufctl->slab, bufctl);

  /* Try to have first_free always pointing at the slab that has the
     most number of free objects.  So after this deallocation, update
     the first_free pointer if reference counter drops below the
     current reference counter of first_free.  */
  if (!space->first_free 
      || slab->refcount < space->first_free->refcount)
    space->first_free = slab;

  pthread_mutex_unlock (&space->lock);
}
