/* Hierarchial argument parsing, layered over getopt

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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
#include <limits.h>		/* for CHAR_BIT */
#include <getopt.h>
#include <cthreads.h>

#include "argp.h"

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

/* ---------------------------------------------------------------- */

#define OPT_PROGNAME	-2

static const struct argp_option argp_default_options[] =
{
  {"help",	  '?',    0, 0,  "Give this help list", -1},
  {"program-name",OPT_PROGNAME,"NAME", OPTION_HIDDEN, "Set the program name"},
  {0, 0}
};

static error_t
argp_default_parser (int key, char *arg, struct argp_state *state)
{
  unsigned usage_flags = ARGP_HELP_STD_HELP;
  switch (key)
    {
    case '?':
      if (state->flags & ARGP_NO_EXIT)
	usage_flags &= ~ARGP_HELP_EXIT;
      argp_help (state->argp, stdout, usage_flags);
      break;

    case OPT_PROGNAME:		/* Set the program name.  */
      program_invocation_name = arg;
      program_invocation_short_name = rindex (arg, '/');
      if (program_invocation_short_name)
	program_invocation_short_name++;
      else
	program_invocation_short_name = program_invocation_name;

      if ((state->flags & (ARGP_PARSE_ARGV0 | ARGP_NO_ERRS))
	  == (ARGP_PARSE_ARGV0 | ARGP_NO_ERRS))
	state->argv[0] = arg;	/* Update what getopt uses too.  */

      break;

    default:
      return EINVAL;
    }
  return 0;
}

static const struct argp argp_default_argp =
  {argp_default_options, &argp_default_parser};


/* ---------------------------------------------------------------- */

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

/* ---------------------------------------------------------------- */

/* Used to regulate access to the getopt routines, which are non-reentrant.  */
static struct mutex getopt_lock = MUTEX_INITIALIZER;

/* This hack to allow programs that know what's going on to call argp
   recursively.  If someday argp is changed not to use the non-reentrant
   getopt interface, we can get rid of this shit.  XXX */
void
_argp_unlock_xxx ()
{
  mutex_unlock (&getopt_lock);
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
};

/* Parse the options strings in ARGC & ARGV according to the argp in
   ARGP.  FLAGS is one of the ARGP_ flags above.  If OPTIND is
   non-NULL, the index in ARGV of the first unparsed option is returned in
   it.  If an unknown option is present, EINVAL is returned; if some parser
   routine returned a non-zero value, it is returned; otherwise 0 is
   returned.  */
error_t
argp_parse (const struct argp *argp, int argc, char **argv, unsigned flags,
	    int *end_index)
{
  error_t err = 0;
  /* True if we think using getopt is still useful; if false, then
     remaining arguments are just passed verbatim with ARGP_KEY_ARG.  This is
     cleared whenever getopt returns KEY_END, but may be set again if the user
     moves the next argument pointer backwards.  */
  int try_getopt = 1;
  /* If true, then err == EINVAL is a result of a non-option argument failing
     to be parsed (which in some cases isn't actually an error).  */
  int arg_einval = 0;
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
  /* A pointer for people to use for iteration over GROUPS.  */
  struct group *group;
  /* State block supplied to parsing routines.  */
  struct argp_state state = { argp, argc, argv, 0, flags, 0, 0 };

  /* Parse the non-option argument ARG, at the current position.  Returns
     any error, and sets ARG_EINVAL to true if return EINVAL.  */
  error_t process_arg (char *val, int *arg_einval)
    {
      int index = state.next;
      error_t err = EINVAL;

      for (group = groups; group < egroup && err == EINVAL; group++)
	if (group->parser)
	  {
	    state.arg_num = group->args_processed;
	    err = (*group->parser)(ARGP_KEY_ARG, val, &state);
	  }

      if (!err)
	if (state.next >= index)
	  /* Remember that we successfully processed a non-option
	     argument -- but only if the user hasn't gotten tricky and set
	     the clock back.  */
	  (--group)->args_processed++;
	else
	  /* The user wants to reparse some args, give getopt another try.  */
	  try_getopt = 1;

      if (err == EINVAL)
	*arg_einval = 1;

      return err;
    }

  /* Parse the option OPT (with argument ARG), at the current position.
     Returns any error, and sets ARG_EINVAL to true if it was actually an
     argument and the parser returned EINVAL.  */
  error_t process_opt (int opt, char *val, int *arg_einval)
    {
      /* The group key encoded in the high bits; 0 for short opts or
	 group_number + 1 for long opts.  */
      int group_key = opt >> USER_BITS;
      error_t err = EINVAL;	/* until otherwise asserted */

      if (group_key == 0)
	/* A short option.  */
	{
	  /* By comparing OPT's position in SHORT_OPTS to the various
	     starting positions in each group's SHORT_END field, we can
	     determine which group OPT came from.  */
	  char *short_index = index (short_opts, opt);
	  if (short_index)
	    for (group = groups; group < egroup; group++)
	      if (group->short_end > short_index && group->parser)
		{
		  err = (*group->parser)(opt, optarg, &state);
		  break;
		}
	}
      else
	/* A long option.  We use shifts instead of masking for extracting
	   the user value in order to preserve the sign.  */
	err =
	  (*groups[group_key - 1].parser)(((opt << GROUP_BITS) >> GROUP_BITS),
					  optarg, &state);
      return err;
    }

  if (! (state.flags & ARGP_NO_HELP))
    /* Add our own options.  */
    {
      const struct argp **plist = alloca (3 * sizeof (struct argp *));
      struct argp *top_argp = alloca (sizeof (struct argp));

      /* TOP_ARGP has no options, it just serves to group the user & default
	 argps.  */
      bzero (top_argp, sizeof (*top_argp));
      top_argp->parents = plist;

      plist[0] = state.argp;
      plist[1] = &argp_default_argp;
      plist[2] = 0;

      state.argp = top_argp;
    }

  /* Find the merged set of getopt options, with keys appropiately prefixed. */
  {
    char *short_end;
    unsigned short_len = (state.flags & ARGP_NO_ARGS) ? 0 : 1;
    struct option *long_end;
    unsigned long_len = 0;
    unsigned num_groups = 0;

    /* For ARGP, increments NUM_GROUPS by the total number of argp structures
       descended from it, and SHORT_LEN & LONG_LEN by the maximum lengths of
       the resulting merged getopt short options string and long-options
       array, respectively.  */
    void calc_lengths (const struct argp *argp)
      {
	const struct argp **parents = argp->parents;
	const struct argp_option *opt = argp->options;

	if (opt || argp->parser)
	  {
	    num_groups++;
	    if (opt)
	      {
		int num_opts = 0;
		while (!_option_is_end (opt++))
		  num_opts++;
		short_len += num_opts * 3; /* opt + up to 2 `:'s */
		long_len += num_opts;
	      }
	  }

	if (parents)
	  while (*parents)
	    calc_lengths (*parents++);
      }

    /* Converts all options in ARGP (which is put in GROUP) and ancestors
       into getopt options stored in SHORT_OPTS and LONG_OPTS; SHORT_END and
       LONG_END are the points at which new options are added.  Returns the
       next unused group entry.  */
    struct group *convert_options (const struct argp *argp,
				   struct group *group)
      {
	/* REAL is the most recent non-alias value of OPT.  */
	const struct argp_option *real = argp->options;
	const struct argp **parents = argp->parents;

	if (real || argp->parser)
	  {
	    const struct argp_option *opt;

	    if (real)
	      for (opt = real; !_option_is_end (opt); opt++)
		{
		  if (! (opt->flags & OPTION_ALIAS))
		    /* OPT isn't an alias, so we can use values from it.  */
		    real = opt;

		  if (_option_is_short (opt))
		    /* OPT can be used as a short option.  */
		    {
		      *short_end++ = opt->key;
		      if (real->arg)
			{
			  *short_end++ = ':';
			  if (real->flags & OPTION_ARG_OPTIONAL)
			    *short_end++ = ':';
			}
		      *short_end = '\0'; /* keep 0 terminated */
		    }

		  if (opt->name && find_long_option (long_opts, opt->name) < 0)
		    /* OPT can be used as a long option.  */
		    {
		      long_end->name = opt->name;
		      long_end->has_arg =
			(real->arg
			 ? (real->flags & OPTION_ARG_OPTIONAL
			    ? optional_argument
			    : required_argument)
			 : no_argument);
		      long_end->flag = 0;
		      /* we add a disambiguating code to all the user's
			 values (which is removed before we actually call
			 the function to parse the value); this means that
			 the user loses use of the high 8 bits in all his
			 values (the sign of the lower bits is preserved
			 however)...  */
		      long_end->val =
			((opt->key | real->key) & USER_MASK)
			  + (((group - groups) + 1) << USER_BITS);

		      /* Keep the LONG_OPTS list terminated.  */
		      (++long_end)->name = NULL;
		    }
		  }

	    group->parser = argp->parser;
	    group->short_end = short_end;
	    group->args_processed = 0;

	    group++;
	  }

	if (parents)
	  while (*parents)
	    group = convert_options (*parents++, group);

	return group;
      }

    calc_lengths (state.argp);

    short_opts = short_end = alloca (short_len + 1);
    if (state.flags & ARGP_IN_ORDER)
      *short_end++ = '-';
    else if (state.flags & ARGP_NO_ARGS)
      *short_end++ = '+';
    *short_end = '\0';

    long_opts = long_end = alloca ((long_len + 1) * sizeof (struct option));
    long_end->name = NULL;

    groups = alloca ((num_groups + 1) * sizeof (struct group));

    egroup = convert_options (state.argp, groups);
  }

  /* Getopt is (currently) non-reentrant.  */
  mutex_lock (&getopt_lock);

  /* Tell getopt to initialize.  */
  state.next = 0;

  if (state.flags & ARGP_NO_ERRS)
    {
      opterr = 0;
      if (state.flags & ARGP_PARSE_ARGV0)
	/* getopt always skips ARGV[0], so we have to fake it out.  As long
	   as opterr is 0, then it shouldn't actually try to access it.  */
	state.argv--, state.argc++;
    }
  else
    opterr = 1;			/* Print error messages.  */

  /* Now use getopt on our coalesced options lists.  */
  while (! err)
    {
      int opt;

      if (state.quoted && state.next < state.quoted)
	/* The next argument pointer has been moved to before the quoted
	   region, so pretend we never saw the quoting `--', and give getopt
	   another chance.  If the user hasn't removed it, getopt will just
	   process it again.  */
	state.quoted = 0;

      if (try_getopt && !state.quoted)
	/* Give getopt a chance to parse this.  */
	{
	  optind = state.next;	/* Put it back in OPTIND for getopt.  */
	  optopt = KEY_END;	/* Distinguish KEY_ERR from a real option.  */
	  opt = getopt_long (state.argc, state.argv, short_opts, long_opts, 0);
	  state.next = optind;	/* And see what getopt did.  */

	  if (opt == KEY_END)
	    /* Getopt says there are no more options, so stop using
	       getopt; we'll continue if necessary on our own.  */
	    {
	      try_getopt = 0;
	      if (state.next > 1
		  && strcmp (state.argv[state.next - 1], QUOTE) == 0)
		/* Not only is this the end of the options, but it's a
		   `quoted' region, which may have args that *look* like
		   options, so we definitely shouldn't try to use getopt past
		   here, whatever happens.  */
		state.quoted = state.next;
	    }
	  else if (opt == KEY_ERR && optopt != KEY_END)
	    /* KEY_ERR can have the same value as a valid user short
	       option, but in the case of a real error, getopt sets OPTOPT
	       to the offending character, which can never be KEY_END.  */
	    {
	      err = EINVAL;
	      break;
	    }
	}
      else
	opt = KEY_END;

      if (opt == KEY_END)
	/* We're past what getopt considers the options.  */
	if (state.next >= state.argc || (state.flags & ARGP_NO_ARGS))
	  break;		/* done */
	else
	  /* A non-option arg.  */
	  err = process_arg (state.argv[state.next++], &arg_einval);
      else if (opt == KEY_ARG)
	/* A non-option argument; try each parser in turn.  */
	err = process_arg (optarg, &arg_einval);
      else
	err = process_opt (opt, optarg, &arg_einval);
    }

  mutex_unlock (&getopt_lock);

  if (err == EINVAL && arg_einval)
    /* Suppress errors generated by unparsed arguments.  */
    err = 0;

  if (!err)
    if (state.next == state.argc)
      /* We successfully parsed all arguments!  Call all the parsers again,
	 just a few more times... */
      {
	for (group = groups; group < egroup && (!err || err ==EINVAL); group++)
	  if (group->args_processed == 0 && group->parser)
	    err = (*group->parser)(ARGP_KEY_NO_ARGS, 0, &state);
	for (group = groups; group < egroup && (!err || err ==EINVAL); group++)
	  if (group->parser)
	    err = (*group->parser)(ARGP_KEY_END, 0, &state);
	if (err == EINVAL)
	  err = 0;		/* Some parser didn't understand.  */
      }
    else if (end_index)
      /* Return any remaining arguments to the user.  */
      *end_index = state.next;
    else
      /* No way to return the remaining arguments, they must be bogus. */
      err = EINVAL;

  if (err && !(state.flags & ARGP_NO_HELP))
    {
      unsigned usage_flags = ARGP_HELP_STD_ERR;
      if (state.flags & ARGP_NO_EXIT)
	usage_flags &= ~ARGP_HELP_EXIT;
      argp_help (state.argp, stderr, usage_flags);
    }

  return err;
}
