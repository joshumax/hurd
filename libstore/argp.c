/* Store argument parsing

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <hurd.h>
#include <argp.h>

#include "store.h"

static const struct argp_option options[] = {
  {"machdev", 'm', 0, 0, "DEVICE is a mach device, not a file"},
  {"interleave", 'i', "BLOCKS", 0, "Interleave in runs of length BLOCKS"},
  {"layer",   'l', 0, 0, "Layer multiple devices for redundancy"},
  {0}
};

static const char args_doc[] = "DEVICE...";
static const char doc[] = "If multiple DEVICEs are specified, they are"
" concatenated unless either --interleave or --layer is specified (mutually"
" exlusive).";

/* Used to hold data during argument parsing.  */
struct store_parse_hook
{
  /* A malloced vector of stores specified on the command line, NUM_STORES
     long.  */
  struct store **stores;
  size_t num_stores;

  /* Pointer to params struct passed in by user.  */
  struct store_argp_params *params;

  off_t interleave;		/* --interleave value */
  int machdev : 1;		/* --machdev specified */
  int layer : 1;		/* --layer specified */
};

/* Free the parse hook H.  If FREE_STORES is true, also free the stores in
   H's store vector, otherwise just free the vector itself.  */
static void
free_hook (struct store_parse_hook *h, int free_stores)
{
  int i;
  if (free_stores)
    for (i = 0; i < h->num_stores; i++)
      store_free (h->stores[i]);
  if (h->stores)
    free (h->stores);
  free (h);
}

static error_t
open_machdev (char *name, struct store_parse_hook *h, struct store **s)
{
  device_t dev_master, device;
  int open_flags = (h->params->readonly ? 0 : D_WRITE) | D_READ;
  error_t err = get_privileged_ports (0, &dev_master);

  if (err)
    return err;

  err = device_open (dev_master, open_flags, name, &device);

  mach_port_deallocate (mach_task_self (), dev_master);

  err = store_device_create (device, s);
  if (err)
    mach_port_deallocate (mach_task_self (), device);

  return err;
}

static error_t
open_file (char *name, struct store_parse_hook *h, struct store **s)
{
  error_t err;
  int open_flags = h->params->readonly ? O_RDONLY : O_RDWR;
  file_t node = file_name_lookup (name, open_flags, 0);

  if (node == MACH_PORT_NULL)
    return errno;

  err = store_create (node, s);
  if (err)
    {
      if (! h->params->no_file_io)
	/* Try making a store that does file io to NODE.  */
	err = store_file_create (node, s);
      if (err)
	mach_port_deallocate (mach_task_self (), node);
    }

  return err;
}

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  error_t err = 0;
  struct store_parse_hook *h = state->hook;

  /* Print a parsing error message and (if exiting is turned off) return the
     error code ERR.  */
#define ERR(err, fmt, args...) \
  do { argp_error (state, fmt , ##args); return err; } while (0)

  switch (opt)
    {
      struct store *s;

    case 'm':
      h->machdev = 1; break;

    case 'i':
      if (h->layer)
	ERR (EINVAL, "--layer and --interleave are exclusive");
      if (h->interleave)
	/* Actually no reason why we couldn't support this.... */
	ERR (EINVAL, "--interleave specified multiple times");

      h->interleave = atoi (arg);
      if (! h->interleave)
	ERR (EINVAL, "%s: Bad value for --interleave", arg);
      break;

    case 'l':
      if (h->interleave)
	ERR (EINVAL, "--layer and --interleave are exclusive");
      h->layer = 1;
      break;

    case ARGP_KEY_ARG:
      /* A store device to use!  */
      if (h->machdev)
	err = open_machdev (arg, h, &s);
      else
	err = open_file (arg, h, &s);
      if (err)
	ERR (err, "%s: %s", arg, strerror (err));
      else
	{
	  struct store **stores = realloc (h->stores, h->num_stores + 1);
	  if (stores)
	    {
	      stores[h->num_stores++] = s;
	      h->stores = stores;
	    }
	  else
	    return ENOMEM;	/* Just fucking lovely */
	}
      break;

    case ARGP_KEY_INIT:
      /* Initialize our parsing state.  */
      if (! state->input)
	return EINVAL;		/* Need at least a way to return a result.  */
      h = malloc (sizeof (struct store_parse_hook));
      if (! h)
	return ENOMEM;
      bzero (h, sizeof (struct store_parse_hook));
      h->params = state->input;
      state->hook = h;
      break;

    case ARGP_KEY_ERROR:
      /* Parsing error occured, free everything. */
      free_hook (h, 1); break;

    case ARGP_KEY_SUCCESS:
      /* Successfully finished parsing, return a result.  */

      if (h->num_stores == 0)
	{
	  free_hook (h, 1);
	  ERR (EINVAL, "No store specified");
	}

      if (state->input == 0)
	/* No way to return a value!  */
	err = EINVAL;
      else if (h->num_stores == 1)
	s = h->stores[0];	/* Just a single store.  */
      else if (h->interleave)
	err =
	  store_ileave_create (h->stores, h->num_stores, h->interleave, &s);
      else if (h->layer)
	{
	  free_hook (h, 1);
	  ERR (EINVAL, "--layer not implemented");
	}
      else
	err = store_concat_create (h->stores, h->num_stores, &s);

      free_hook (h, err);
      if (! err)
	h->params->result = s;

      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

struct argp
store_argp = { options, parse_opt, args_doc, doc };
