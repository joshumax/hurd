/* slab.h - The GNU Hurd slab allocator interface.
   Copyright (C) 2003, 2005 Free Software Foundation, Inc.
   Written by Marcus Brinkmann <marcus@gnu.org>

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

#ifndef _HURD_SLAB_H
#define _HURD_SLAB_H	1

#include <errno.h>
#include <stdbool.h>
#include <pthread.h>


/* Allocate a buffer in *PTR of size SIZE which must be a power of 2
   and self aligned (i.e. aligned on a SIZE byte boundary).  HOOK is
   as provided to hurd_slab_create.  Return 0 on success, an error
   code on failure.  */
typedef error_t (*hurd_slab_allocate_buffer_t) (void *hook, size_t size,
						void **ptr);

/* Deallocate buffer BUFFER of size SIZE.  HOOK is as provided to
   hurd_slab_create.  */
typedef error_t (*hurd_slab_deallocate_buffer_t) (void *hook, void *buffer,
						  size_t size);

/* Initialize the slab object pointed to by OBJECT.  HOOK is as
   provided to hurd_slab_create.  */
typedef error_t (*hurd_slab_constructor_t) (void *hook, void *object);

/* Destroy the slab object pointed to by OBJECT.  HOOK is as provided
   to hurd_slab_create.  */
typedef void (*hurd_slab_destructor_t) (void *hook, void *object);


/* The type of a slab space.  

   The structure is divided into two parts: the first is only used
   while the slab space is constructed.  Its fields are either
   initialized by a static initializer (HURD_SLAB_SPACE_INITIALIZER)
   or by the hurd_slab_create function.  The initialization of the
   space is delayed until the first allocation.  After that only the
   second part is used.  */

typedef struct hurd_slab_space *hurd_slab_space_t;
struct hurd_slab_space
{
  /* First part.  Used when initializing slab space object.   */
  
  /* True if slab space has been initialized.  */
  bool initialized;
  
  /* Protects this structure, along with all the slabs.  No need to
     delay initialization of this field.  */
  pthread_mutex_t lock;

  /* The size and alignment of objects allocated using this slab
     space.  These to fields are used to calculate the final object
     size, which is put in SIZE (defined below).  */
  size_t requested_size;
  size_t requested_align;

  /* The size of each slab. */
  size_t slab_size;

  /* The buffer allocator.  */
  hurd_slab_allocate_buffer_t allocate_buffer;

  /* The buffer deallocator.  */
  hurd_slab_deallocate_buffer_t deallocate_buffer;

  /* The constructor.  */
  hurd_slab_constructor_t constructor;

  /* The destructor.  */
  hurd_slab_destructor_t destructor;

  /* The user's private data.  */
  void *hook;

  /* Second part.  Runtime information for the slab space.  */

  struct hurd_slab *slab_first;
  struct hurd_slab *slab_last;

  /* In the doubly-linked list of slabs, empty slabs come first, after
     that the slabs that have some buffers allocated, and finally the
     complete slabs (refcount == 0).  FIRST_FREE points to the first
     non-empty slab.  */
  struct hurd_slab *first_free;

  /* For easy checking, this holds the value the reference counter
     should have for an empty slab.  */
  int full_refcount;

  /* The size of one object.  Should include possible alignment as
     well as the size of the bufctl structure.  */
  size_t size;
};


/* Static initializer.  TYPE is used to get size and alignment of
   objects the slab space will be used to allocate.  ALLOCATE_BUFFER
   may be NULL in which case mmap is called.  DEALLOCATE_BUFFER may be
   NULL in which case munmap is called.  CTOR and DTOR are the slab's
   object constructor and destructor, respectivly and may be NULL if
   not required.  HOOK is passed as user data to the constructor and
   destructor.  */
#define HURD_SLAB_SPACE_INITIALIZER(TYPE, ALLOC, DEALLOC, CTOR,	\
				    DTOR, HOOK)			\
  {								\
    false,							\
    PTHREAD_MUTEX_INITIALIZER, 					\
    sizeof (TYPE),						\
    __alignof__ (TYPE),						\
    ALLOC,							\
    DEALLOC,							\
    CTOR,							\
    DTOR,							\
    HOOK							\
    /* The rest of the structure will be filled with zeros,     \
       which is good for us.  */				\
  }


/* Create a new slab space with the given object size, alignment,
   constructor and destructor.  ALIGNMENT can be zero.
   ALLOCATE_BUFFER may be NULL in which case mmap is called.
   DEALLOCATE_BUFFER may be NULL in which case munmap is called.  CTOR
   and DTOR are the slabs object constructor and destructor,
   respectivly and may be NULL if not required.  HOOK is passed as the
   first argument to the constructor and destructor.  */
error_t hurd_slab_create (size_t size, size_t alignment,
			  hurd_slab_allocate_buffer_t allocate_buffer,
			  hurd_slab_deallocate_buffer_t deallocate_buffer,
			  hurd_slab_constructor_t constructor,
			  hurd_slab_destructor_t destructor,
			  void *hook,
			  hurd_slab_space_t *space);

/* Destroy all objects and the slab space SPACE.  If there were no
   outstanding allocations free the slab space.  Returns EBUSY if
   there are still allocated objects in the slab space.  The dual of
   hurd_slab_create.  */
error_t hurd_slab_free (hurd_slab_space_t space);

/* Like hurd_slab_create, but does not allocate storage for the slab.  */
error_t hurd_slab_init (hurd_slab_space_t space, size_t size, size_t alignment,
			hurd_slab_allocate_buffer_t allocate_buffer,
			hurd_slab_deallocate_buffer_t deallocate_buffer,
			hurd_slab_constructor_t constructor,
			hurd_slab_destructor_t destructor,
			void *hook);

/* Destroy all objects and the slab space SPACE.  Returns EBUSY if
   there are still allocated objects in the slab.  The dual of
   hurd_slab_init.  */
error_t hurd_slab_destroy (hurd_slab_space_t space);

/* Allocate a new object from the slab space SPACE.  */
error_t hurd_slab_alloc (hurd_slab_space_t space, void **buffer);

/* Deallocate the object BUFFER from the slab space SPACE.  */
void hurd_slab_dealloc (hurd_slab_space_t space, void *buffer);

/* Create a more strongly typed slab interface a la a C++ template.

   NAME is the name of the new slab class.  NAME is used to synthesize
   names for the class types and methods using the following rule: the
   hurd_ namespace will prefix all method names followed by NAME
   followed by an underscore and finally the method name.  The
   following are thus exposed:

    Types:
     struct hurd_NAME_slab_space
     hurd_NAME_slab_space_t

     error_t (*hurd_NAME_slab_constructor_t) (void *hook, element_type *buffer)
     void (*hurd_NAME_slab_destructor_t) (void *hook, element_type *buffer)

    Functions:
     error_t hurd_NAME_slab_create (hurd_slab_allocate_buffer_t
                                     allocate_buffer,
                                    hurd_slab_deallocate_buffer_t
                                     deallocate_buffer,
                                    hurd_NAME_slab_constructor_t constructor,
                                    hurd_NAME_slab_destructor_t destructor,
                                    void *hook,
                                    hurd_NAME_slab_space_t *space);
     error_t hurd_NAME_slab_free (hurd_NAME_slab_space_t space);

     error_t hurd_NAME_slab_init (hurd_NAME_slab_space_t space,
                                  hurd_slab_allocate_buffer_t allocate_buffer,
                                  hurd_slab_deallocate_buffer_t
                                   deallocate_buffer,
                                  hurd_NAME_slab_constructor_t constructor,
                                  hurd_NAME_slab_destructor_t destructor,
                                  void *hook);
     error_t hurd_NAME_slab_destroy (hurd_NAME_slab_space_t space);

     error_t hurd_NAME_slab_alloc (hurd_NAME_slab_space_t space,
                                   element_type **buffer);
     void hurd_NAME_slab_dealloc (hurd_NAME_slab_space_t space,
                                  element_type *buffer);

  ELEMENT_TYPE is the type of elements to store in the slab.  If you
  want the slab to contain struct foo, pass `struct foo' as the
  ELEMENT_TYPE (not `struct foo *'!!!).
     
*/
#define SLAB_CLASS(name, element_type)                                       \
struct hurd_##name##_slab_space						     \
{									     \
  struct hurd_slab_space space;						     \
};									     \
typedef struct hurd_##name##_slab_space *hurd_##name##_slab_space_t;	     \
									     \
typedef error_t (*hurd_##name##_slab_constructor_t) (void *hook,	     \
						     element_type *buffer);  \
									     \
typedef void (*hurd_##name##_slab_destructor_t) (void *hook,		     \
						 element_type *buffer);	     \
									     \
static inline error_t							     \
hurd_##name##_slab_create (hurd_slab_allocate_buffer_t allocate_buffer,	     \
			   hurd_slab_deallocate_buffer_t deallocate_buffer,  \
			   hurd_##name##_slab_constructor_t constructor,     \
			   hurd_##name##_slab_destructor_t destructor,	     \
			   void *hook,					     \
			   hurd_##name##_slab_space_t *space)		     \
{									     \
  union									     \
  {									     \
    hurd_##name##_slab_constructor_t t;					     \
    hurd_slab_constructor_t u;						     \
  } con;								     \
  union									     \
  {									     \
    hurd_##name##_slab_destructor_t t;					     \
    hurd_slab_destructor_t u;						     \
  } des;								     \
  union									     \
  {									     \
    hurd_##name##_slab_space_t *t;					     \
    hurd_slab_space_t *u;						     \
  } foo;								     \
  con.t = constructor;							     \
  des.t = destructor;							     \
  foo.t = space;							     \
									     \
  return hurd_slab_create(sizeof (element_type), __alignof__ (element_type), \
			  allocate_buffer, deallocate_buffer,		     \
			  con.u, des.u, hook, foo.u);			     \
}									     \
									     \
static inline error_t							     \
hurd_##name##_slab_free (hurd_##name##_slab_space_t space)		     \
{									     \
  return hurd_slab_free (&space->space);				     \
}									     \
									     \
static inline error_t							     \
hurd_##name##_slab_init (hurd_##name##_slab_space_t space,		     \
			 hurd_slab_allocate_buffer_t allocate_buffer,	     \
			 hurd_slab_deallocate_buffer_t deallocate_buffer,    \
			 hurd_##name##_slab_constructor_t constructor,	     \
			 hurd_##name##_slab_destructor_t destructor,	     \
			 void *hook)					     \
{									     \
  union									     \
  {									     \
    hurd_##name##_slab_constructor_t t;					     \
    hurd_slab_constructor_t u;						     \
  } con;								     \
  union									     \
  {									     \
    hurd_##name##_slab_destructor_t t;					     \
    hurd_slab_destructor_t u;						     \
  } des;								     \
  con.t = constructor;							     \
  des.t = destructor;							     \
									     \
  return hurd_slab_init (&space->space,					     \
			 sizeof (element_type), __alignof__ (element_type),  \
			 allocate_buffer, deallocate_buffer,		     \
			 con.u, des.u, hook);				     \
}									     \
									     \
static inline error_t							     \
hurd_##name##_slab_destroy (hurd_##name##_slab_space_t space)		     \
{									     \
  return hurd_slab_destroy (&space->space);				     \
}									     \
									     \
static inline error_t							     \
hurd_##name##_slab_alloc (hurd_##name##_slab_space_t space,		     \
			  element_type **buffer)			     \
{									     \
  union									     \
  {									     \
    element_type **e;							     \
    void **v;								     \
  } foo;								     \
  foo.e = buffer;							     \
									     \
  return hurd_slab_alloc (&space->space, foo.v);			     \
}									     \
									     \
static inline void							     \
hurd_##name##_slab_dealloc (hurd_##name##_slab_space_t space,		     \
			    element_type *buffer)			     \
{									     \
  union									     \
  {									     \
    element_type *e;							     \
    void *v;								     \
  } foo;								     \
  foo.e = buffer;							     \
									     \
  hurd_slab_dealloc (&space->space, foo.v);				     \
}

#endif	/* _HURD_SLAB_H */
