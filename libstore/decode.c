/* Store wire decoding

   Copyright (C) 1996,97,98,2001,02 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <string.h>
#include <malloc.h>

#include "store.h"

/* The maximum number of runs for which we allocate run vectors on the stack. */
#define MAX_STACK_RUNS (16*1024 / sizeof (struct store_run))

/* Decodes the standard leaf encoding that's common to various builtin
   formats, and calls CREATE to actually create the store.  */
error_t
store_std_leaf_decode (struct store_enc *enc,
		       store_std_leaf_create_t create,
		       struct store **store)
{
  char *misc, *name;
  error_t err;
  int flags;
  mach_port_t port;
  size_t block_size, num_runs, name_len, misc_len;
  /* Call CREATE appriately from within store_with_decoded_runs.  */
  error_t call_create (const struct store_run *runs, size_t num_runs)
    {
      return (*create)(port, flags, block_size, runs, num_runs, store);
    }

  /* Make sure there are enough encoded ints and ports.  */
  if (enc->cur_int + 6 > enc->num_ints || enc->cur_port + 1 > enc->num_ports)
    return EINVAL;

  /* Read encoded ints.  */
  enc->cur_int++; /* Ignore type.  */
  flags = enc->ints[enc->cur_int++];
  block_size = enc->ints[enc->cur_int++];
  num_runs = enc->ints[enc->cur_int++];
  name_len = enc->ints[enc->cur_int++];
  misc_len = enc->ints[enc->cur_int++];

  /* Make sure there are enough encoded offsets and data.  */
  if (enc->cur_offset + num_runs * 2 > enc->num_offsets
      || enc->cur_data + name_len + misc_len > enc->data_len)
    return EINVAL;

  if (name_len > 0 && enc->data[enc->cur_data + name_len - 1] != '\0')
    return EINVAL;		/* Name not terminated.  */

  if (name_len > 0)
    {
      name = strdup (enc->data + enc->cur_data);
      if (! name)
	return ENOMEM;
      enc->cur_data += name_len;
    }
  else
    name = 0;

  if (misc_len > 0)
    {
      misc = malloc (misc_len);
      if (! misc)
	{
	  if (name)
	    free (name);
	  return ENOMEM;
	}
      memcpy (misc, enc->data + enc->cur_data + name_len, misc_len);
      enc->cur_data += misc_len;
    }
  else
    misc = 0;

  /* Read encoded ports (be careful to deallocate this if we barf).  */
  port = enc->ports[enc->cur_port++];

  err = store_with_decoded_runs (enc, num_runs, call_create);
  if (err)
    {
      mach_port_deallocate (mach_task_self (), port);
      if (misc)
	free (misc);
      if (name)
	free (name);
    }
  else
    {
      (*store)->flags = flags;
      (*store)->name = name;
      (*store)->misc = misc;
      (*store)->misc_len = misc_len;
    }

  return err;
}

/* Call FUN with the vector RUNS of length RUNS_LEN extracted from ENC.  */
error_t
store_with_decoded_runs (struct store_enc *enc, size_t num_runs,
			 error_t (*fun) (const struct store_run *runs,
					 size_t num_runs))
{
  int i;
  error_t err;

  /* Since the runs are passed in an array of off_t pairs, and we use struct
     store_run, we have to make a temporary array to hold the (probably
     bitwise identical) converted representation to pass to CREATE.  */
  if (num_runs <= MAX_STACK_RUNS)
    {
      struct store_run runs[num_runs];
      off_t *e = enc->offsets + enc->cur_offset;
      for (i = 0; i < num_runs; i++)
	{
	  runs[i].start = *e++;
	  runs[i].length = *e++;
	}
      enc->cur_offset = e - enc->offsets;
      err = (*fun)(runs, num_runs);
    }
  else
    /* Ack.  Too many runs to allocate the temporary RUNS array on the stack.
       This will probably never happen.  */
    {
      struct store_run *runs = malloc (num_runs * sizeof (struct store_run));
      if (runs)
	{
	  off_t *e = enc->offsets + enc->cur_offset;
	  for (i = 0; i < num_runs; i++)
	    {
	      runs[i].start = *e++;
	      runs[i].length = *e++;
	    }
	  enc->cur_offset = e - enc->offsets;
	  err = (*fun) (runs, num_runs);
	  free (runs);
	}
      else
	err = ENOMEM;
    }

  return err;
}

/* Decode ENC, either returning a new store in STORE, or an error.  CLASSES
   defines the mapping from hurd storage class ids to store classes; if it is
   0, STORE_STD_CLASSES is used.  If nothing else is to be done with ENC, its
   contents may then be freed using store_enc_dealloc.  */
error_t
store_decode (struct store_enc *enc, const struct store_class *const *classes,
	      struct store **store)
{
  const struct store_class *const *cl;

  if (enc->cur_int >= enc->num_ints)
    /* The first int should always be the type.  */
    return EINVAL;

  if (enc->ints[enc->cur_int] == STORAGE_NETWORK)
    /* This is a special case because store classes supporting
       individual URL types will also use STORAGE_NETWORK,
       and we want the generic dispatcher to come first.  */
    return store_url_decode (enc, classes, store);

  for (cl = classes ?: __start_store_std_classes;
       classes ? *cl != 0 : cl < __stop_store_std_classes;
       ++cl)
    if ((*cl)->id == enc->ints[enc->cur_int])
      {
	if ((*cl)->decode)
	  return (*(*cl)->decode) (enc, classes, store);
	else
	  return EOPNOTSUPP;
      }

# pragma weak store_module_decode
  if (! classes && store_module_decode)
    {
      error_t err = store_module_decode (enc, classes, store);
      if (err != ENOENT)
	return err;
    }

  return EINVAL;
}
