/* Store argument parsing

   Copyright (C) 1996,97,98,99,2001,02 Free Software Foundation, Inc.
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

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <string.h>
#include <stdlib.h>
#include <assert-backtrace.h>
#include <hurd.h>
#include <argp.h>
#include <argz.h>

#include "store.h"

/* We use this class variable instead of just the name so that we ensure
   linking in store_open to define it.  */
#define DEFAULT_STORE_CLASS store_query_class

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
  /* Names of devices parsed.  */
  char *names;
  size_t names_len;

  /* Prefix that should be applied to each member of NAMES.  */
  char *name_prefix;

  /* --store-type specified.  This defaults to the `query' type.  */
  const struct store_class *type;

  /* A vector of class pointers used to lookup class names.  Defaults to
     STORE_STD_CLASSES.  */
  const struct store_class *const *classes;

  /* DEFAULT_TYPE field passed to parser.  */
  const struct store_class *default_type;

  store_offset_t interleave;	/* --interleave value */
  int layer : 1;		/* --layer specified */
};

void
store_parsed_free (struct store_parsed *parsed)
{
  if (parsed->names_len > 0)
    free (parsed->names);
  if (parsed->name_prefix)
    free (parsed->name_prefix);
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
	snprintf (buf, sizeof buf, "--interleave=%Ld", parsed->interleave);
      else
	snprintf (buf, sizeof buf, "--layer=%d", parsed->layer);
      err = argz_add (args, args_len, buf);
    }

  if (!err && parsed->type != parsed->default_type)
    {
      if (parsed->name_prefix)
	/* A name prefix of "PFX" is equivalent to appending ":PFX" to the
	   type name.  */
	{
	  size_t npfx_len = strlen (parsed->name_prefix);
	  char tname[strlen ("--store-type=")
		    + strlen (parsed->type->name) + 1 + npfx_len + 1];
	  snprintf (tname, sizeof tname, "--store-type=%s:%.*s",
		    parsed->type->name, (int) npfx_len, parsed->name_prefix);
	  err = argz_add (args, args_len, tname);
	}
      else
	/* A simple type name.  */
	{
	  snprintf (buf, sizeof buf, "--store-type=%s", parsed->type->name);
	  err = argz_add (args, args_len, buf);
	}
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
    {
      if (parsed->interleave)
	{
	  snprintf (buf, sizeof buf, "interleave(%Ld,", parsed->interleave);
	  pfx = buf;
	}
      else if (parsed->layer)
	pfx = "layer(";
    }

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
  size_t pfx_len = parsed->name_prefix ? strlen (parsed->name_prefix) : 0;
  size_t num = argz_count (parsed->names, parsed->names_len);

  error_t open (char *name, struct store **store)
    {
      const struct store_class *type = parsed->type;
      if (type->open)
	{
	  if (parsed->name_prefix)
	    /* If there's a name prefix, we prefix any names we open with that
	       and a colon.  */
	    {
	      char pfxed_name[pfx_len + 1 + strlen (name) + 1];
	      stpcpy (stpcpy (stpcpy (pfxed_name, parsed->name_prefix),
			      ":"),
		      name);
	      return (*type->open) (pfxed_name, flags, parsed->classes, store);
	    }
	  else
	    return (*type->open) (name, flags, parsed->classes, store);
	}
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
	{
	  if (parsed->interleave)
	    err =
	      store_ileave_create (stores, num, parsed->interleave,
				   flags, store);
	  else if (parsed->layer)
	    assert_backtrace (! parsed->layer);
	  else
	    err = store_concat_create (stores, num, flags, store);
	}

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
find_class (const char *name, const struct store_class *const *const classes)
{
  const struct store_class *const *cl;
  for (cl = classes ?: __start_store_std_classes;
       classes ? *cl != 0 : cl < __stop_store_std_classes;
       ++cl)
    if ((*cl)->name && strcmp (name, (*cl)->name) == 0)
      return *cl;

# pragma weak store_module_find_class
  if (! classes && store_module_find_class)
    {
      const struct store_class *cl;
      if (store_module_find_class (name, strchr (name, '\0'), &cl) == 0)
	return cl;
    }

  return 0;
}

/* Print a parsing error message and (if exiting is turned off) return the
   error code ERR.  Requires a variable called STATE to be in scope.  */
#define PERR(err, fmt, args...) \
  do { argp_error (state, fmt , ##args); return err; } while (0)

/* Parse a --store-type/-T option.  */
static error_t
parse_type (char *arg, struct argp_state *state, struct store_parsed *parsed)
{
  char *name_prefix = 0;
  char *type_name = arg;
  const struct store_class *type;
  char *class_sep = strchr (arg, ':');

  if (class_sep)
    /* A `:'-separated class name "T1:T2" is equivalent to prepending "T2:"
       to the device name passed to T1, and is useful for the case where T1
       takes typed names of the form "T:NAME".  A trailing `:', like "T1:" is
       equivalent to prefixing `:' to the device name, which causes NAME to
       be opened with store_open, as a file.  */
    {
      type_name = strndupa (arg, class_sep - arg);
      name_prefix = class_sep + 1;
    }

  type = find_class (type_name, parsed->classes);
  if (!type || !type->open)
    PERR (EINVAL, "%s: Invalid argument to --store-type", arg);
  else if (type != parsed->type && parsed->type != parsed->default_type)
    PERR (EINVAL, "--store-type specified multiple times");

  parsed->type = type;
  parsed->name_prefix = name_prefix;

  return 0;
}

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  error_t err;
  struct store_parsed *parsed = state->hook;

  switch (opt)
    {
    case 'm':
      arg = "device";
      /* fall through */
    case 'T':
      return parse_type (arg, state, parsed);

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
	memset (parsed, 0, sizeof(struct store_parsed));
	parsed->classes = params->classes;
	parsed->default_type =
	  find_class (params->default_type ?: DEFAULT_STORE_CLASS.name,
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
      /* Parsing error occurred, free everything. */
      store_parsed_free (parsed); break;

    case ARGP_KEY_SUCCESS:
      /* Successfully finished parsing, return a result.  */
      if (parsed->names == 0
	  && (!parsed->type->validate_name
	      || (*parsed->type->validate_name) (0, parsed->classes) != 0))
	{
	  struct store_argp_params *params = state->input;
	  store_parsed_free (parsed);
	  if (!params->store_optional)
	    PERR (EINVAL, "No store specified");
	  parsed = 0;
	}
      ((struct store_argp_params *)state->input)->result = parsed;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

struct argp
store_argp = { options, parse_opt, args_doc, doc };
