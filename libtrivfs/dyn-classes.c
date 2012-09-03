/* Dynamically allocated port classes/buckets recognized by trivfs

   Copyright (C) 1997 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "priv.h"

/* Auxiliary info for each vector element.  */
struct aux
{
  void (*free_el)();
  unsigned refs;
};

/* Vectors of dynamically allocated port classes/buckets.  */

/* Protid port classes.  */
struct port_class **trivfs_dynamic_protid_port_classes = 0;
size_t trivfs_num_dynamic_protid_port_classes = 0;
static struct aux *dynamic_protid_port_classes_aux = 0;
static size_t dynamic_protid_port_classes_sz = 0;

/* Control port classes.  */
struct port_class **trivfs_dynamic_control_port_classes = 0;
size_t trivfs_num_dynamic_control_port_classes = 0;
static struct aux *dynamic_control_port_classes_aux = 0;
static size_t dynamic_control_port_classes_sz = 0;

/* Port buckets.  */
struct port_bucket **trivfs_dynamic_port_buckets = 0;
size_t trivfs_num_dynamic_port_buckets = 0;
static struct aux *dynamic_port_buckets_aux = 0;
static size_t dynamic_port_buckets_sz = 0;

/* Lock used to control access to all the above vectors.  */
static pthread_mutex_t dyn_lock = PTHREAD_MUTEX_INITIALIZER;


/* Add EL to the vector pointed to by VEC_V, which should point to a vector
   of pointers of some type, and has a length stored in *SZ; If there's
   already a pointer to EL in VEC_V, nothing will actually be added, but the
   reference count for that element will be increased.  *NUM is the actual
   number of non-null elements in the vector, and will be incremented if
   something is actually added.  AUX_VEC is a pointer to a vector of struct
   aux elements, that contains information parralleling VEC_V.  FREE_EL, if
   non-zero, should be a function that takes a single argument of the same
   type as EL, and deallocates it; this function is called in the following
   cases: (1) An error is encountered trying to grow one of the vectors, (2)
   when the element is eventually freed by drop_el.  */
static error_t
add_el (void *el, void (*free_el)(),
	void *vec_v, struct aux **aux_vec,
	size_t *sz, size_t *num)
{
  int i;
  size_t new_sz;
  void ***vec, **new_vec;
  struct aux *new_aux_vec;

  if (! el)
    return ENOMEM;

  pthread_mutex_lock (&dyn_lock);

  vec = vec_v;

  for (i = 0; i < *sz; i++)
    if (! (*vec)[i])
      {
	(*vec)[i] = el;
	(*aux_vec)[i].free_el = free_el;
	(*aux_vec)[i].refs = 1;
	(*num)++;
	pthread_mutex_unlock (&dyn_lock);
	return 0;
      }
    else if ((*vec)[i] == el)
      {
	(*aux_vec)[i].refs++;
	pthread_mutex_unlock (&dyn_lock);
	return 0;
      }

  new_sz = *sz + 4;
  new_vec = realloc (*vec, new_sz * sizeof (void *));
  new_aux_vec = realloc (*aux_vec, new_sz * sizeof (struct aux));

  if (!new_vec || !new_aux_vec)
    {
      if (free_el)
	(*free_el) (el);
      /* One of the vectors might be the wrong size, but who cares.  */
      return ENOMEM;
    }

  for (i = *sz; i < new_sz;  i++)
    new_vec[i] = 0;

  new_vec[*sz] = el;
  new_aux_vec[*sz].free_el = free_el;
  new_aux_vec[*sz].refs = 1;
  (*num)++;

  *vec = new_vec;
  *aux_vec = new_aux_vec;
  *sz = new_sz;

  pthread_mutex_unlock (&dyn_lock);

  return 0;
}

/* Scan VEC_V, which should be a vector of SZ pointers of the same type as
   EL, for EL; if it is found, then decrement its reference count, and if
   that goes to zero, decrement *NUM and free EL if it had an associated free
   routine passed to add_el.  */
static void
drop_el (void *el, void *vec_v, struct aux *aux_vec,
	 size_t sz, size_t *num)
{
  int i;
  void **vec;

  if (! el)
    return;

  pthread_mutex_lock (&dyn_lock);

  vec = vec_v;

  for (i = 0; i < sz; i++)
    if (vec[i] == el)
      {
	if (aux_vec[i].refs == 1)
	  {
	    if (aux_vec[i].free_el)
	      (*aux_vec[i].free_el) (el);
	    vec[i] = 0;
	    (*num)--;
	  }
	else
	  aux_vec[i].refs--;
	break;
      }

  pthread_mutex_unlock (&dyn_lock);
}

/* Add the port class *CLASS to the list of control port classes recognized
   by trivfs; if *CLASS is 0, an attempt is made to allocate a new port
   class, which is stored in *CLASS.  */
error_t
trivfs_add_control_port_class (struct port_class **class)
{
  /* XXX Gee, there *is no* way of freeing port classes or buckets!  So we
     actually never free anything!  */

  if (! *class)
    {
      *class = ports_create_class (trivfs_clean_cntl, 0);
      if (! *class)
	return ENOMEM;
    }

  return
    add_el (*class, 0,
	    &trivfs_dynamic_control_port_classes, 
	    &dynamic_control_port_classes_aux, 
	    &dynamic_control_port_classes_sz, 
	    &trivfs_num_dynamic_control_port_classes);
}

/* Remove the previously added dynamic control port class CLASS, freeing it
   if it was allocated by trivfs_add_control_port_class.  */
void
trivfs_remove_control_port_class (struct port_class *class)
{
  drop_el (class, 
	   trivfs_dynamic_control_port_classes, 
	   dynamic_control_port_classes_aux, 
	   dynamic_control_port_classes_sz, 
	   &trivfs_num_dynamic_control_port_classes);
}

/* Add the port class *CLASS to the list of protid port classes recognized by
   trivfs; if *CLASS is 0, an attempt is made to allocate a new port class,
   which is stored in *CLASS.  */
error_t
trivfs_add_protid_port_class (struct port_class **class)
{
  /* XXX Gee, there *is no* way of freeing port classes or buckets!  So we
     actually never free anything!  */

  if (! *class)
    {
      *class = ports_create_class (trivfs_clean_protid, 0);
      if (! *class)
	return ENOMEM;
    }

  return
    add_el (*class, 0,
	    &trivfs_dynamic_protid_port_classes, 
	    &dynamic_protid_port_classes_aux, 
	    &dynamic_protid_port_classes_sz, 
	    &trivfs_num_dynamic_protid_port_classes);
}

/* Remove the previously added dynamic protid port class CLASS, freeing it
   if it was allocated by trivfs_add_protid_port_class.  */
void
trivfs_remove_protid_port_class (struct port_class *class)
{
  drop_el (class, 
	   trivfs_dynamic_protid_port_classes, 
	   dynamic_protid_port_classes_aux, 
	   dynamic_protid_port_classes_sz, 
	   &trivfs_num_dynamic_protid_port_classes);
}

/* Add the port bucket *BUCKET to the list of dynamically allocated port
   buckets; if *bucket is 0, an attempt is made to allocate a new port
   bucket, which is then stored in *bucket.  */
error_t
trivfs_add_port_bucket (struct port_bucket **bucket)
{
  /* XXX Gee, there *is no* way of freeing port bucketes or buckets!  So we
     actually never free anything!  */

  if (! *bucket)
    {
      *bucket = ports_create_bucket ();
      if (! *bucket)
	return ENOMEM;
    }

  return
    add_el (*bucket, 0,
	    &trivfs_dynamic_port_buckets, 
	    &dynamic_port_buckets_aux, 
	    &dynamic_port_buckets_sz, 
	    &trivfs_num_dynamic_port_buckets);
}

/* Remove the previously added dynamic port bucket BUCKET, freeing it
   if it was allocated by trivfs_add_port_bucket.  */
void
trivfs_remove_port_bucket (struct port_bucket *bucket)
{
  drop_el (bucket, 
	   trivfs_dynamic_port_buckets, 
	   dynamic_port_buckets_aux, 
	   dynamic_port_buckets_sz, 
	   &trivfs_num_dynamic_port_buckets);
}
