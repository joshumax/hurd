/* Store argument parsing

   Copyright (C) 1996, 1997 Free Software Foundation, Inc.
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
   59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <string.h>
#include <assert.h>
#include <hurd.h>
#include <argp.h>
#include <argz.h>

#include "store.h"

#define DEFAULT_STORE_TYPE "query"

static const struct argp_option options[] = {
  {"store-type",'T', "TYPE",   0, "Each DEVICE names a store of type TYPE"},
  {"machdev",	'm', 0,        OPTION_HIDDEN}, /* deprecated */
  {"interleave",'I', "BLOCKS", 0, "Interleave in runs of length BLOCKS"},
  {"layer",   	'L', 0,        0, "Layer multiple devices for redundancy"},
  {0}
};

static const char args_doc[] = "DEVICE...";
static const char doc[] = "\vIf neither --interleave or --layer is specified,"
" multiple DEVICEs are concatenated.";

struct store_parsed
{
  char *names;
  size_t names_len;

  const struct store_class *const *classes; /* CLASSES field passed to parser.  */

  const struct store_class *type;	/* --store-type specified */
  const struct store_class *default_type; /* DEFAULT_TYPE field passed to parser.  */

  off_t interleave;		/* --interleave value */
  int layer : 1;		/* --layer specified */
};

void
store_parsed_free (struct store_parsed *parsed)
{
  if (parsed->names_len > 0)
    free (parsed->names);
  free (parsed);
}

/* Add the arguments  PARSED, and return the corresponding store in STORE.  */
error_t
store_parsed_append_args (const struct store_parsed *parsed,
			  char **args, size_t *args_len)
{
  char buf[40];
  error_t err = 0;
  size_t num_names = argz_count (parsed->names, parsed->names_len);

  if (!err && num_names > 1 && (parsed->interleave || parsed->layer))
    {
      if (parsed->interleave)
	snprintf (buf, sizeof buf, "--interleave=%ld", parsed->interleave);
      else
	snprintf (buf, sizeof buf, "--layer=%ld", parsed->layer);
      err = argz_add (args, args_len, buf);
    }

  if (!err && parsed->type != parsed->default_type)
    {
      snprintf (buf, sizeof buf, "--store-type=%s", parsed->type->name);
      err = argz_add (args, args_len, buf);
    }

  if (! err)
    err = argz_append (args, args_len, parsed->names, parsed->names_len);

  return err;
}

error_t
store_parsed_name (const struct store_parsed *parsed, char **name)
{
  char buf[40];
  char *pfx = 0;

  if (argz_count (parsed->names, parsed->names_len) > 1)
    if (parsed->interleave)
      {
	snprintf (buf, sizeof buf, "interleave(%ld,", parsed->interleave);
	pfx = buf;
      }
    else if (parsed->layer)
      pfx = "layer(";

  if (pfx)
    *name = malloc (strlen (pfx) + parsed->names_len + 1);
  else
    *name = malloc (parsed->names_len);

  if (! *name)
    return ENOMEM;

  if (pfx)
    {
      char *end = stpcpy (*name, pfx);
      bcopy (parsed->names, end, parsed->names_len);
      argz_stringify (end, parsed->names_len, ',');
      strcpy (end + parsed->names_len, ")");
    }
  else
    {
      bcopy (parsed->names, *name, parsed->names_len);
      argz_stringify (*name, parsed->names_len, ',');
    }

  return 0;
}

/* Open PARSED, and return the corresponding store in STORE.  */
error_t
store_parsed_open (const struct store_parsed *parsed, int flags,
		   struct store **store)
{
  size_t num = argz_count (parsed->names, parsed->names_len);
  error_t open (char *name, struct store **store)
    {
      const struct store_class *type = parsed->type;
      if (type->open)
	return (*type->open) (name, flags, parsed->classes, store);
      else
	return EOPNOTSUPP;
    }

  if (num == 1)
    return open (parsed->names, store);
  else if (num == 0)
    return open (0, store);
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
static const struct store_class *
find_class (const char *name, const struct store_class *const *classes)
{
  while (*classes)
    if ((*classes)->name && strcmp (name, (*classes)->name) == 0)
      return *classes;
    else
      classes++;
  return 0;
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
    case 'm':
      arg = "device";
      /* fall through */
    case 'T':
      {
	const struct store_class *type = find_class (arg, parsed->classes);
	if (!type || !type->open)
	  PERR (EINVAL, "%s: Invalid argument to --store-type", arg);
	else if (type != parsed->type && parsed->type != parsed->default_type)
	  PERR (EINVAL, "--store-type specified multiple times");
	else
	  parsed->type = type;
      }
      break;

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
      if (parsed->type->validate_name)
	err = (*parsed->type->validate_name) (arg, parsed->classes);
      else
	err = 0;
      if (! err)
	err = argz_add (&parsed->names, &parsed->names_len, arg);
      if (err)
	argp_failure (state, 1, err, "%s", arg);
      return err;
      break;

    case ARGP_KEY_INIT:
      /* Initialize our parsing state.  */
      {
	struct store_argp_params *params = state->input;
	if (! params)
	  return EINVAL;	/* Need at least a way to return a result.  */
	parsed = state->hook = malloc (sizeof (struct store_parsed));
	if (! parsed)
	  return ENOMEM;
	bzero (parsed, sizeof (struct store_parsed));
	parsed->classes = params->classes ?: store_std_classes;
	parsed->default_type =
	  find_class (params->default_type ?: DEFAULT_STORE_TYPE,
		      parsed->classes);
	if (! parsed->default_type)
	  {
	    free (parsed);
	    return EINVAL;
	  }
	parsed->type = parsed->default_type;
      }
      break;

    case ARGP_KEY_ERROR:
      /* Parsing error occured, free everything. */
      store_parsed_free (parsed); break;

    case ARGP_KEY_SUCCESS:
      /* Successfully finished parsing, return a result.  */
      if (parsed->names == 0
	  && (!parsed->type->validate_name
	      || (*parsed->type->validate_name) (0, parsed->classes) != 0))
	{
	  store_parsed_free (parsed);
	  PERR (EINVAL, "No store specified");
	}
      else
	((struct store_argp_params *)state->input)->result = parsed;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

struct argp
store_argp = { options, parse_opt, args_doc, doc };
