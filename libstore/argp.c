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

#include <string.h>
#include <assert.h>
#include <hurd.h>
#include <argp.h>
#include <argz.h>

#include "store.h"

static const struct argp_option options[] = {
  {"device",	'd', 0,        0, "DEVICE is a mach device, not a file"},
  {"interleave",'I', "BLOCKS", 0, "Interleave in runs of length BLOCKS"},
  {"layer",   	'L', 0,        0, "Layer multiple devices for redundancy"},
  {0}
};

static const char args_doc[] = "DEVICE...";
static const char doc[] = "\vIf multiple DEVICEs are specified, they are"
" concatenated unless either --interleave or --layer is specified (mutually"
" exlusive).";

struct store_parsed
{
  char *names;
  size_t names_len;
  off_t interleave;		/* --interleave value */
  int machdev : 1;		/* --machdev specified */
  int layer : 1;		/* --layer specified */
};

void
store_parsed_free (struct store_parsed *parsed)
{
  free (parsed->names);
  free (parsed);
}

/* Add the arguments  PARSED, and return the corresponding store in STORE.  */
error_t
store_parsed_append_args (const struct store_parsed *parsed,
			  char **args, size_t *args_len)
{
  error_t err = 0;
  size_t num_names = argz_count (parsed->names, parsed->names_len);

  if (parsed->machdev)
    err = argz_add (args, args_len, "--machdev");

  if (!err && num_names > 1 && (parsed->interleave || parsed->layer))
    {
      char buf[40];
      if (parsed->interleave)
	snprintf (buf, sizeof buf, "--interleave=%ld", parsed->interleave);
      else
	snprintf (buf, sizeof buf, "--layer=%ld", parsed->layer);
      err = argz_add (args, args_len, buf);
    }

  if (! err)
    err = argz_append (args, args_len, parsed->names, parsed->names_len);

  return err;
}

/* Open PARSED, and return the corresponding store in STORE.  */
error_t
store_parsed_open (const struct store_parsed *parsed, int flags,
		   struct store_class *classes,
		   struct store **store)
{
  size_t num = argz_count (parsed->names, parsed->names_len);
  error_t open (char *name, struct store **store)
    {
      if (parsed->machdev)
	return store_device_open (name, flags, store);
      else
	return store_open (name, flags, classes, store);
    }

  if (num == 1)
    return open (parsed->names, store);
  else
    {
      int i;
      char *name;
      error_t err = 0;
      struct store **stores = malloc (sizeof (struct store *) * num);

      if (! stores)
	return ENOMEM;

      for (i = 0, name = parsed->names;
	   !err && i < num;
	   i++, name = argz_next (parsed->names, parsed->names_len, name))
	err = open (name, &stores[i]);

      if (! err)
	if (parsed->interleave)
	  err =
	    store_ileave_create (stores, num, parsed->interleave,
				 flags, store);
	else if (parsed->layer)
	  assert (! parsed->layer);
	else
	  err = store_concat_create (stores, num, flags, store);

      if (err)
	{
	  while (i > 0)
	    store_free (stores[i--]);
	  free (stores);
	}

      return err;
    }
}

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  error_t err;
  struct store_parsed *parsed = state->hook;

  /* Print a parsing error message and (if exiting is turned off) return the
     error code ERR.  */
#define PERR(err, fmt, args...) \
  do { argp_error (state, fmt , ##args); return err; } while (0)

  switch (opt)
    {
    case 'd':
      parsed->machdev = 1; break;

    case 'I':
      if (parsed->layer)
	PERR (EINVAL, "--layer and --interleave are exclusive");
      if (parsed->interleave)
	/* Actually no reason why we couldn't support this.... */
	PERR (EINVAL, "--interleave specified multiple times");

      parsed->interleave = atoi (arg);
      if (! parsed->interleave)
	PERR (EINVAL, "%s: Bad value for --interleave", arg);
      break;

    case 'L':
#if 1
      argp_failure (state, 5, 0, "--layer not implemented");
      return EINVAL;
#else
      if (parsed->interleave)
	PERR (EINVAL, "--layer and --interleave are exclusive");
      parsed->layer = 1;
#endif
      break;

    case ARGP_KEY_ARG:
      /* A store device to use!  */
      err = argz_add (&parsed->names, &parsed->names_len, arg);
      if (err)
	argp_failure (state, 1, err, "%s", arg);
      return err;
      break;

    case ARGP_KEY_INIT:
      /* Initialize our parsing state.  */
      if (! state->input)
	return EINVAL;		/* Need at least a way to return a result.  */
      state->hook = malloc (sizeof (struct store_parsed));
      if (! state->hook)
	return ENOMEM;
      bzero (state->hook, sizeof (struct store_parsed));
      break;

    case ARGP_KEY_ERROR:
      /* Parsing error occured, free everything. */
      store_parsed_free (parsed); break;

    case ARGP_KEY_SUCCESS:
      /* Successfully finished parsing, return a result.  */
      if (parsed->names == 0)
	{
	  store_parsed_free (parsed);
	  PERR (EINVAL, "No store specified");
	}
      else
	*(struct store_parsed **)state->input = parsed;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

struct argp
store_argp = { options, parse_opt, args_doc, doc };
