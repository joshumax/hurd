/* Hierarchial argument parsing, layered over getopt

   Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.

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

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <getopt.h>

#if _LIBC - 0
#include <libc-lock.h>
#else
#ifdef HAVE_CTHREADS_H
#include <cthreads.h>
#endif
#endif /* _LIBC */

#include "argp.h"
#include "argp-namefrob.h"

/* Getopt return values.  */
#define KEY_END (-1)		/* The end of the options.  */
#define KEY_ARG 1		/* A non-option argument.  */
#define KEY_ERR '?'		/* An error parsing the options.  */

/* The meta-argument used to prevent any further arguments being interpreted
   as options.  */
#define QUOTE "--"

/* The number of bits we steal in a long-option value for our own use.  */
#define GROUP_BITS CHAR_BIT

/* The number of bits available for the user value.  */
#define USER_BITS ((sizeof ((struct option *)0)->val * CHAR_BIT) - GROUP_BITS)
#define USER_MASK ((1 << USER_BITS) - 1)

/* EZ alias for ARGP_ERR_UNKNOWN.  */
#define EBADKEY ARGP_ERR_UNKNOWN

/* Default options.  */

/* When argp is given the --HANG switch, _ARGP_HANG is set and argp will sleep
   for one second intervals, decrementing _ARGP_HANG until it's zero.  Thus
   you can force the program to continue by attaching a debugger and setting
   it to 0 yourself.  */
volatile int _argp_hang = 0;

#define OPT_PROGNAME	-2
#define OPT_USAGE	-3
#define OPT_HANG	-4

static const struct argp_option argp_default_options[] =
{
  {"help",	  '?',    	0, 0,  "Give this help list", -1},
  {"usage",	  OPT_USAGE,	0, 0,  "Give a short usage message"},
  {"program-name",OPT_PROGNAME,"NAME", OPTION_HIDDEN, "Set the program name"},
  {"HANG",	  OPT_HANG,    "SECS", OPTION_ARG_OPTIONAL | OPTION_HIDDEN,
     "Hang for SECS seconds (default 3600)"},
  {0, 0}
};

static error_t
argp_default_parser (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case '?':
      __argp_state_help (state, state->out_stream, ARGP_HELP_STD_HELP);
      break;
    case OPT_USAGE:
      __argp_state_help (state, state->out_stream,
		       ARGP_HELP_USAGE | ARGP_HELP_EXIT_OK);
      break;

    case OPT_PROGNAME:		/* Set the program name.  */
      program_invocation_name = arg;

      /* [Note that some systems only have PROGRAM_INVOCATION_SHORT_NAME (aka
	 __PROGNAME), in which case, PROGRAM_INVOCATION_NAME is just defined
	 to be that, so we have to be a bit careful here.]  */
      arg = strrchr (arg, '/');
      if (arg)
	program_invocation_short_name = arg + 1;
      else
	program_invocation_short_name = program_invocation_name;

      if ((state->flags & (ARGP_PARSE_ARGV0 | ARGP_NO_ERRS))
	  == ARGP_PARSE_ARGV0)
	state->argv[0] = program_invocation_name; /* Update what getopt uses too.  */

      break;

    case OPT_HANG:
      _argp_hang = atoi (arg ? arg : "3600");
      while (_argp_hang-- > 0)
	__sleep (1);
      break;

    default:
      return EBADKEY;
    }
  return 0;
}

static const struct argp argp_default_argp =
  {argp_default_options, &argp_default_parser};


static const struct argp_option argp_version_options[] =
{
  {"version",	  'V',    	0, 0,  "Print program version", -1},
  {0, 0}
};

static error_t
argp_version_parser (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'V':
      if (argp_program_version_hook)
	(*argp_program_version_hook) (state->out_stream, state);
      else if (argp_program_version)
	fprintf (state->out_stream, "%s\n", argp_program_version);
      else
	__argp_error (state, "No version known!?");
      if (! (state->flags & ARGP_NO_EXIT))
	exit (0);
      break;
    default:
      return EBADKEY;
    }
  return 0;
}

static const struct argp argp_version_argp =
  {argp_version_options, &argp_version_parser};

/* Returns the offset into the getopt long options array LONG_OPTIONS of a
   long option with called NAME, or -1 if none is found.  Passing NULL as
   NAME will return the number of options.  */
static int
find_long_option (struct option *long_options, const char *name)
{
  struct option *l = long_options;
  while (l->name != NULL)
    if (name != NULL && strcmp (l->name, name) == 0)
      return l - long_options;
    else
      l++;
  if (name == NULL)
    return l - long_options;
  else
    return -1;
}

/* If we can, we regulate access to getopt, which is non-reentrant, with a
   mutex.  Since the case we're trying to guard against is two different
   threads interfering, and it's possible that someone might want to call
   argp_parse recursively (they're careful), we use a recursive lock if
   possible.  */

#if _LIBC - 0

__libc_lock_define_initialized_recursive (static, getopt_lock)
#define LOCK_GETOPT   __libc_lock_lock_recursive (getopt_lock)
#define UNLOCK_GETOPT __libc_lock_unlock_recursive (getopt_lock)

#else /* !_LIBC */
#ifdef HAVE_CTHREADS_H

static struct mutex getopt_lock = MUTEX_INITIALIZER;
#define LOCK_GETOPT   mutex_lock (&getopt_lock)
#define UNLOCK_GETOPT mutex_unlock (&getopt_lock)

#else /* !HAVE_CTHREADS_H */

#define LOCK_GETOPT    (void)0
#define UNLOCK_GETOPT  (void)0

#endif /* HAVE_CTHREADS_H */
#endif /* _LIBC */

/* This hack to allow programs that know what's going on to call argp
   recursively.  If someday argp is changed not to use the non-reentrant
   getopt interface, we can get rid of this shit.  XXX */
void
_argp_unlock_xxx ()
{
  UNLOCK_GETOPT;
}

/* The state of a `group' during parsing.  Each group corresponds to a
   particular argp structure from the tree of such descending from the top
   level argp passed to argp_parse.  */
struct group
{
  /* This group's parsing function.  */
  argp_parser_t parser;

  /* Points to the point in SHORT_OPTS corresponding to the end of the short
     options for this group.  We use it to determine from which group a
     particular short options is from.  */
  char *short_end;

  /* The number of non-option args sucessfully handled by this parser.  */
  unsigned args_processed;

  /* This group's parser's parent's group.  */
  struct group *parent;
  unsigned parent_index;	/* And the our position in the parent.   */

  /* These fields are swapped into and out of the state structure when
     calling this group's parser.  */
  void *input, **child_inputs;
  void *hook;
};

/* Call GROUP's parser with KEY and ARG, swapping any group-specific info
   from STATE before calling, and back into state afterwards.  If GROUP has
   no parser, EBADKEY is returned.  */
static error_t
group_parse (struct group *group, struct argp_state *state, int key, char *arg)
{
  if (group->parser)
    {
      error_t err;
      state->hook = group->hook;
      state->input = group->input;
      state->child_inputs = group->child_inputs;
      state->arg_num = group->args_processed;
      err = (*group->parser)(key, arg, state);
      group->hook = state->hook;
      return err;
    }
  else
    return EBADKEY;
}

struct parser
{
  const struct argp *argp;

  /* SHORT_OPTS is the getopt short options string for the union of all the
     groups of options.  */
  char *short_opts;
  /* LONG_OPTS is the array of getop long option structures for the union of
     all the groups of options.  */
  struct option *long_opts;

  /* States of the various parsing groups.  */
  struct group *groups;
  /* The end of the GROUPS array.  */
  struct group *egroup;
  /* An vector containing storage for the CHILD_INPUTS field in all groups.  */
  void **child_inputs;

  /* True if we think using getopt is still useful; if false, then
     remaining arguments are just passed verbatim with ARGP_KEY_ARG.  This is
     cleared whenever getopt returns KEY_END, but may be set again if the user
     moves the next argument pointer backwards.  */
  int try_getopt;

  /* State block supplied to parsing routines.  */
  struct argp_state state;

  /* Memory used by this parser.  */
  void *storage;
};

/* The next usable entries in the various parser tables being filled in by
   convert_options.  */
struct parser_convert_state
{
  struct parser *parser;
  char *short_end;
  struct option *long_end;
  void **child_inputs_end;
};

/* Converts all options in ARGP (which is put in GROUP) and ancestors
   into getopt options stored in SHORT_OPTS and LONG_OPTS; SHORT_END and
   CVT->LONG_END are the points at which new options are added.  Returns the
   next unused group entry.  CVT holds state used during the conversion.  */
static struct group *
convert_options (const struct argp *argp,
		 struct group *parent, unsigned parent_index,
		 struct group *group, struct parser_convert_state *cvt)
{
  /* REAL is the most recent non-alias value of OPT.  */
  const struct argp_option *real = argp->options;
  const struct argp_child *children = argp->children;

  if (real || argp->parser)
    {
      const struct argp_option *opt;

      if (real)
	for (opt = real; !__option_is_end (opt); opt++)
	  {
	    if (! (opt->flags & OPTION_ALIAS))
	      /* OPT isn't an alias, so we can use values from it.  */
	      real = opt;

	    if (! (real->flags & OPTION_DOC))
	      /* A real option (not just documentation).  */
	      {
		if (__option_is_short (opt))
		  /* OPT can be used as a short option.  */
		  {
		    *cvt->short_end++ = opt->key;
		    if (real->arg)
		      {
			*cvt->short_end++ = ':';
			if (real->flags & OPTION_ARG_OPTIONAL)
			  *cvt->short_end++ = ':';
		      }
		    *cvt->short_end = '\0'; /* keep 0 terminated */
		  }

		if (opt->name
		    && find_long_option (cvt->parser->long_opts, opt->name) < 0)
		  /* OPT can be used as a long option.  */
		  {
		    cvt->long_end->name = opt->name;
		    cvt->long_end->has_arg =
		      (real->arg
		       ? (real->flags & OPTION_ARG_OPTIONAL
			  ? optional_argument
			  : required_argument)
		       : no_argument);
		    cvt->long_end->flag = 0;
		    /* we add a disambiguating code to all the user's
		       values (which is removed before we actually call
		       the function to parse the value); this means that
		       the user loses use of the high 8 bits in all his
		       values (the sign of the lower bits is preserved
		       however)...  */
		    cvt->long_end->val =
		      ((opt->key | real->key) & USER_MASK)
		      + (((group - cvt->parser->groups) + 1) << USER_BITS);

		    /* Keep the LONG_OPTS list terminated.  */
		    (++cvt->long_end)->name = NULL;
		  }
	      }
	    }

      group->parser = argp->parser;
      group->short_end = cvt->short_end;
      group->args_processed = 0;
      group->parent = parent;
      group->parent_index = parent_index;
      group->input = 0;
      group->hook = 0;
      group->child_inputs = 0;

      if (children)
	/* Assign GROUP's CHILD_INPUTS field some space from
           CVT->child_inputs_end.*/
	{
	  unsigned num_children = 0;
	  while (children[num_children].argp)
	    num_children++;
	  group->child_inputs = cvt->child_inputs_end;
	  cvt->child_inputs_end += num_children;
	}

      parent = group++;
    }
  else
    parent = 0;

  if (children)
    {
      unsigned index = 0;
      while (children->argp)
	group =
	  convert_options (children++->argp, parent, index++, group, cvt);
    }

  return group;
}

/* Find the merged set of getopt options, with keys appropiately prefixed. */
static void
parser_convert (struct parser *parser, const struct argp *argp, int flags)
{
  struct parser_convert_state cvt;

  cvt.parser = parser;
  cvt.short_end = parser->short_opts;
  cvt.long_end = parser->long_opts;
  cvt.child_inputs_end = parser->child_inputs;

  if (flags & ARGP_IN_ORDER)
    *cvt.short_end++ = '-';
  else if (flags & ARGP_NO_ARGS)
    *cvt.short_end++ = '+';
  *cvt.short_end = '\0';

  cvt.long_end->name = NULL;

  parser->argp = argp;

  if (argp)
    parser->egroup = convert_options (argp, 0, 0, parser->groups, &cvt);
  else
    parser->egroup = parser->groups; /* No parsers at all! */
}

/* Lengths of various parser fields which we will allocated.  */
struct parser_sizes
{
  size_t short_len;		/* Getopt short options string.  */
  size_t long_len;		/* Getopt long options vector.  */
  size_t num_groups;		/* Group structures we allocate.  */
  size_t num_child_inputs;	/* Child input slots.  */
};

/* For ARGP, increments the NUM_GROUPS field in SZS by the total number of
 argp structures descended from it, and the SHORT_LEN & LONG_LEN fields by
 the maximum lengths of the resulting merged getopt short options string and
 long-options array, respectively.  */
static void
calc_sizes (const struct argp *argp,  struct parser_sizes *szs)
{
  const struct argp_child *child = argp->children;
  const struct argp_option *opt = argp->options;

  if (opt || argp->parser)
    {
      szs->num_groups++;
      if (opt)
	{
	  int num_opts = 0;
	  while (!__option_is_end (opt++))
	    num_opts++;
	  szs->short_len += num_opts * 3; /* opt + up to 2 `:'s */
	  szs->long_len += num_opts;
	}
    }

  if (child)
    while (child->argp)
      {
	calc_sizes ((child++)->argp, szs);
	szs->num_child_inputs++;
      }
}

/* Initializes PARSER to parse ARGP in a manner described by FLAGS.  */
static error_t
parser_init (struct parser *parser, const struct argp *argp,
	     int argc, char **argv, int flags, void *input)
{
  error_t err = 0;
  struct group *group;
  struct parser_sizes szs;

  szs.short_len = (flags & ARGP_NO_ARGS) ? 0 : 1;
  szs.long_len = 0;
  szs.num_groups = 0;
  szs.num_child_inputs = 0;

  if (argp)
    calc_sizes (argp, &szs);

  /* Lengths of the various bits of storage used by PARSER.  */
#define GLEN (szs.num_groups + 1) * sizeof (struct group)
#define CLEN (szs.num_child_inputs * sizeof (void *))
#define LLEN ((szs.long_len + 1) * sizeof (struct option))
#define SLEN (szs.short_len + 1)

  parser->storage = malloc (GLEN + CLEN + LLEN + SLEN);
  if (! parser->storage)
    return ENOMEM;

  parser->groups = parser->storage;
  parser->child_inputs = parser->storage + GLEN;
  parser->long_opts = parser->storage + GLEN + CLEN;
  parser->short_opts = parser->storage + GLEN + CLEN + LLEN;

  memset (parser->child_inputs, 0, szs.num_child_inputs * sizeof (void *));
  parser_convert (parser, argp, flags);

  parser->try_getopt = 1;

  memset (&parser->state, 0, sizeof (struct argp_state));
  parser->state.argp = parser->argp;
  parser->state.argc = argc;
  parser->state.argv = argv;
  parser->state.err_stream = stderr;
  parser->state.out_stream = stdout;
  parser->state.next = 0;	/* Tell getopt to initialize.  */

  /* Call each parser for the first time, giving it a chance to propagate
     values to child parsers.  */
  if (parser->groups < parser->egroup)
    parser->groups->input = input;
  for (group = parser->groups;
       group < parser->egroup && (!err || err == EBADKEY);
       group++)
    {
      if (group->parent)
	/* If a child parser, get the initial input value from the parent. */
	group->input = group->parent->child_inputs[group->parent_index];
      err = group_parse (group, &parser->state, ARGP_KEY_INIT, 0);
    }
  if (err == EBADKEY)
    err = 0;			/* Some parser didn't understand.  */

  if (err)
    return err;

  if (parser->state.argv == argv && argv[0])
    /* There's an argv[0]; use it for messages.  */
    parser->state.name = argv[0];
  else
    parser->state.name = program_invocation_name;

  /* Getopt is (currently) non-reentrant.  */
  LOCK_GETOPT;

  if (parser->state.flags & ARGP_NO_ERRS)
    {
      __opterr = 0;
      if (parser->state.flags & ARGP_PARSE_ARGV0)
	/* getopt always skips ARGV[0], so we have to fake it out.  As long
	   as OPTERR is 0, then it shouldn't actually try to access it.  */
	parser->state.argv--, parser->state.argc++;
    }
  else
    __opterr = 1;		/* Print error messages.  */

  return 0;
}

/* Free any storage consumed by PARSER (but not PARSER itself).  */
static error_t
parser_finalize (struct parser *parser,
		 error_t err, int arg_ebadkey, int *end_index)
{
  struct group *group;

  UNLOCK_GETOPT;

  if (err == EBADKEY && arg_ebadkey)
    /* Suppress errors generated by unparsed arguments.  */
    err = 0;

  if (!err)
    if (parser->state.next == parser->state.argc)
      /* We successfully parsed all arguments!  Call all the parsers again,
	 just a few more times... */
      {
	for (group = parser->groups;
	     group < parser->egroup && (!err || err==EBADKEY);
	     group++)
	  if (group->args_processed == 0)
	    err = group_parse (group, &parser->state, ARGP_KEY_NO_ARGS, 0);
	for (group = parser->groups;
	     group < parser->egroup && (!err || err==EBADKEY);
	     group++)
	  err = group_parse (group, &parser->state, ARGP_KEY_END, 0);

	if (err == EBADKEY)
	  err = 0;		/* Some parser didn't understand.  */
      }
    else if (end_index)
      /* Return any remaining arguments to the user.  */
      *end_index = parser->state.next;
    else
      /* No way to return the remaining arguments, they must be bogus. */
      {
	if (!(parser->state.flags & ARGP_NO_ERRS) && parser->state.err_stream)
	  fprintf (parser->state.err_stream,
		   "%s: Too many arguments\n", parser->state.name);
	err = EBADKEY;
      }

  /* Okay, we're all done, with either an error or success.  We only call the
     parsers once more, to indicate which one.  */

  if (err)
    {
      /* Maybe print an error message.  */
      if (err == EBADKEY)
	__argp_state_help (&parser->state, parser->state.err_stream,
			   ARGP_HELP_STD_ERR);

      /* Since we didn't exit, give each parser an error indication.  */
      for (group = parser->groups; group < parser->egroup; group++)
	group_parse (group, &parser->state, ARGP_KEY_ERROR, 0);
    }
  else
    /* Do final cleanup, including propagating back values from parsers.  */
    {
      /* We pass over the groups in reverse order so that child groups are
	 given a chance to do there processing before passing back a value to
	 the parent.  */
      for (group = parser->egroup - 1
	   ; group >= parser->groups && (!err || err == EBADKEY)
	   ; group--)
	err = group_parse (group, &parser->state, ARGP_KEY_SUCCESS, 0);
      if (err == EBADKEY)
	err = 0;		/* Some parser didn't understand.  */
    }

  if (err == EBADKEY)
    err = EINVAL;

  free (parser->storage);

  return err;
}
  
/* Parse the non-option argument ARG, at the current position.  Returns
   any error, and sets ARG_EBADKEY to true if return EBADKEY.  */
static error_t
parser_parse_arg (struct parser *parser, char *val, int *arg_ebadkey)
{
  int index = parser->state.next;
  error_t err = EBADKEY;
  struct group *group;

  for (group = parser->groups
       ; group < parser->egroup && err == EBADKEY
       ; group++)
    err = group_parse (group, &parser->state, ARGP_KEY_ARG, val);

  if (!err)
    if (parser->state.next >= index)
      /* Remember that we successfully processed a non-option
	 argument -- but only if the user hasn't gotten tricky and set
	 the clock back.  */
      (--group)->args_processed++;
    else
      /* The user wants to reparse some args, give getopt another try.  */
      parser->try_getopt = 1;

  if (err == EBADKEY)
    *arg_ebadkey = 1;

  return err;
}

/* Parse the option OPT (with argument ARG), at the current position.
   Returns any error, and sets ARG_EBADKEY to true if it was actually an
   argument and the parser returned EBADKEY.  */
static error_t
parser_parse_opt (struct parser *parser, int opt, char *val, int *arg_ebadkey)
{
  /* The group key encoded in the high bits; 0 for short opts or
     group_number + 1 for long opts.  */
  int group_key = opt >> USER_BITS;
  error_t err = EBADKEY;	/* until otherwise asserted */
  struct group *group;

  if (group_key == 0)
    /* A short option.  */
    {
      /* By comparing OPT's position in SHORT_OPTS to the various
	 starting positions in each group's SHORT_END field, we can
	 determine which group OPT came from.  */
      char *short_index = strchr (parser->short_opts, opt);
      if (short_index)
	for (group = parser->groups; group < parser->egroup; group++)
	  if (group->short_end > short_index)
	    {
	      err = group_parse (group, &parser->state, opt, optarg);
	      break;
	    }
      else
	err = EBADKEY;
    }
  else
    /* A long option.  We use shifts instead of masking for extracting
       the user value in order to preserve the sign.  */
    err =
      group_parse (&parser->groups[group_key - 1], &parser->state,
		   (opt << GROUP_BITS) >> GROUP_BITS, optarg);

  return err;
}

/* Parse the next argument in PARSER (as indicated by PARSER->state.next.  */
static error_t
parser_parse_next (struct parser *parser, int *arg_ebadkey)
{
  int opt;

  if (parser->state.quoted && parser->state.next < parser->state.quoted)
    /* The next argument pointer has been moved to before the quoted
       region, so pretend we never saw the quoting `--', and give getopt
       another chance.  If the user hasn't removed it, getopt will just
       process it again.  */
    parser->state.quoted = 0;

  if (parser->try_getopt && !parser->state.quoted)
    /* Give getopt a chance to parse this.  */
    {
      __optind = parser->state.next; /* Put it back in OPTIND for getopt.  */
      __optopt = KEY_END;	/* Distinguish KEY_ERR from a real option.  */
      if (parser->state.flags & ARGP_LONG_ONLY)
	opt = __getopt_long_only (parser->state.argc, parser->state.argv,
				  parser->short_opts, parser->long_opts, 0);
      else
	opt = __getopt_long (parser->state.argc, parser->state.argv,
			     parser->short_opts, parser->long_opts, 0);
      parser->state.next = __optind; /* And see what getopt did.  */

      if (opt == KEY_END)
	/* Getopt says there are no more options, so stop using
	   getopt; we'll continue if necessary on our own.  */
	{
	  parser->try_getopt = 0;
	  if (parser->state.next > 1
	      && strcmp (parser->state.argv[parser->state.next - 1], QUOTE)
	           == 0)
	    /* Not only is this the end of the options, but it's a
	       `quoted' region, which may have args that *look* like
	       options, so we definitely shouldn't try to use getopt past
	       here, whatever happens.  */
	    parser->state.quoted = parser->state.next;
	}
      else if (opt == KEY_ERR && __optopt != KEY_END)
	/* KEY_ERR can have the same value as a valid user short
	   option, but in the case of a real error, getopt sets OPTOPT
	   to the offending character, which can never be KEY_END.  */
	{
	  *arg_ebadkey = 0;
	  return EBADKEY;
	}
    }
  else
    opt = KEY_END;

  if (opt == KEY_END)
    /* We're past what getopt considers the options.  */
    if (parser->state.next >= parser->state.argc
	|| (parser->state.flags & ARGP_NO_ARGS))
      /* Indicate that we're done.  */
      {
	*arg_ebadkey = 1;
	return EBADKEY;
      }
    else
      /* A non-option arg.  */
      return
	parser_parse_arg (parser,
			       parser->state.argv[parser->state.next++],
			       arg_ebadkey);
  else if (opt == KEY_ARG)
    /* A non-option argument; try each parser in turn.  */
    return parser_parse_arg (parser, optarg, arg_ebadkey);
  else
    return parser_parse_opt (parser, opt, optarg, arg_ebadkey);

#if 0
  if (err == EBADKEY && arg_ebadkey)
    state.next--;		/* Put back the unused argument.  */
#endif
}

/* Parse the options strings in ARGC & ARGV according to the argp in ARGP.
   FLAGS is one of the ARGP_ flags above.  If END_INDEX is non-NULL, the
   index in ARGV of the first unparsed option is returned in it.  If an
   unknown option is present, EINVAL is returned; if some parser routine
   returned a non-zero value, it is returned; otherwise 0 is returned.  */
error_t
__argp_parse (const struct argp *argp, int argc, char **argv, unsigned flags,
	      int *end_index, void *input)
{
  error_t err;
  struct parser parser;

  /* If true, then err == EBADKEY is a result of a non-option argument failing
     to be parsed (which in some cases isn't actually an error).  */
  int arg_ebadkey = 0;

  if (! (flags & ARGP_NO_HELP))
    /* Add our own options.  */
    {
      struct argp_child *child = alloca (4 * sizeof (struct argp_child));
      struct argp *top_argp = alloca (sizeof (struct argp));

      /* TOP_ARGP has no options, it just serves to group the user & default
	 argps.  */
      memset (top_argp, 0, sizeof (*top_argp));
      top_argp->children = child;

      memset (child, 0, 4 * sizeof (struct argp_child));

      if (argp)
	(child++)->argp = argp;
      (child++)->argp = &argp_default_argp;
      if (argp_program_version || argp_program_version_hook)
	(child++)->argp = &argp_version_argp;
      child->argp = 0;

      argp = top_argp;
    }

  /* Construct a parser for these arguments.  */
  err = parser_init (&parser, argp, argc, argv, flags, input);

  if (! err)
    /* Parse! */
    {
      while (! err)
	err = parser_parse_next (&parser, &arg_ebadkey);
      err = parser_finalize (&parser, err, arg_ebadkey, end_index);
    }

  return err;
}
#ifdef weak_alias
weak_alias (__argp_parse, argp_parse)
#endif
