/* Hierarchial options parsing, layered over getopt

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#include "options.h"

#define EOF (-1)

/* The number of bits we steal in a long-option value for our own use.  */
#define GROUP_BITS CHAR_BIT

/* The number of bits available for the user value.  */
#define USER_BITS ((sizeof ((struct option *)0)->val * CHAR_BIT) - GROUP_BITS)
#define USER_MASK ((1 << USER_BITS) - 1)

/* ---------------------------------------------------------------- */

/* Returns the offset into LONG_OPTIONS of a long option with called NAME, or
   -1 if none is found.  Passing NULL as NAME will return the number of
   options.  */
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

/* Parse the options strings in ARGC & ARGV according to the options in
   OPTIONS.  FLAGS is one of the OPTIONS_ flags above.  If OPTIND is
   non-NULL, the index in ARGV of the first unparsed option is returned in
   it.  If an unknown option is present, EINVAL is returned; if some parser
   routine returned a non-zero value, it is returned; otherwise 0 is
   returned.  */
error_t
options_parse (struct options *options,
	       int argc, char **argv,
	       unsigned flags, int *arg_index)
{
  int opt;
  struct options *o;
  /* SHORT_OPTS is the getopt short options string for the union of all the
     groups of options.  */
  char *short_opts;
  /* GROUP_SHORT_STARTS is an array pointing to the part of SHORT_OPTS
     corresponding to each different group of options.  We use it to
     determine from which groupa particular short options is from.  */
  char **group_short_starts;
  /* LONG_OPTS is the array of getop long option structures for the union of
     all the groups of options.  */
  struct option *long_opts;
  error_t err = 0;

  /* Find the merged set of short options.  */
  {
    char *short_end;
    int short_len = (flags & OPTIONS_PARSE_ARGS) ? 1 : 0;
    int num_groups = 0, group;

    /* Find the (maximum) amount of space necessary to store all combined
       short options, plus the number of options groups in the chain.  */
    for (o = options; o != NULL; o = o->parent)
      {
	num_groups++;
	short_len += strlen (o->short_options);
      }

    short_opts = short_end = alloca (short_len + 1);
    if (flags & OPTIONS_PARSE_ARGS)
      *short_end++ = '-';	/* Tell getopt we want to do this.  */
    *short_end = '\0';

    group_short_starts = alloca (num_groups * sizeof (char *));

    for (o = options, group = 0; o != NULL; o = o->parent, group++)
      {
	char *s;

	group_short_starts[group] = short_end;

	for (s = o->short_options; *s != '\0'; s++)
	  /* We add *S to our set of short options only if it hasn't already
	     been added by some previous group.  */
	  if (*s != ':' && !index (short_opts, *s))
	    {
	      *short_end++ = *s;
	      /* Copy all the colon modifiers following the option.  */
	      while (s[1] == ':')
		*short_end++ = *++s;
	      *short_end = '\0';
	    }
      }
  }

  /* Find the merged set of long options, with keys appropiately prefixed. */
  {
    struct option *long_end;
    int group;
    int long_len = 0;

    for (o = options; o != NULL; o = o->parent)
      long_len += find_long_option (o->long_options, NULL);

    long_opts = long_end = alloca ((long_len + 1) * sizeof (struct option));
    long_end->name = NULL;

    /* Note that GROUP starts at 1 because 0 is for short options.  */
    for (o = options, group = 1; o != NULL; o = o->parent, group++)
      {
	struct option *l;
	for (l = o->long_options; l->name != NULL; l++)
	  /* Only add the long option L if it hasn't been already.  */
	  if (find_long_option (long_opts, l->name) < 0)
	    {
	      *long_end = *l;
	      if (long_end->flag == NULL)
		/* In the case where a long option returns a key from getopt,
		   we add a disambiguating code to all the user's values
		   (which is removed before we actually call the function to
		   parse the value); this means that the user loses use of
		   the high 8 bits in all his values (the sign of the lower
		   bits is preserved however)...  */
		long_end->val = (l->val & USER_MASK) + (group << USER_BITS);
	      /* Keep the LONG_OPTS list terminated.  */
	      (++long_end)->name = NULL;
	    }
      }
  }

  /* Getopt is (currently) non-reentrant.  */
  mutex_lock (&getopt_lock);

  /* Tell getopt to initialize.  */
  optind = 0;

  if (flags & OPTIONS_PRINT_ERRS)
    opterr = 1;			/* Print error messages.  */
  else
    {
      opterr = 0;
      if (!(flags & OPTIONS_SKIP_ARG0))
	/* getopt always skips ARGV[0], so we have to fake it out.  As long
	   as opterr is 0, then it shouldn't actually try to access it.  */
	argv--, argc++;
    }

  /* Now use getopt on our coalesced options lists.  */
  while ((opt = getopt_long (argc, argv, short_opts, long_opts, 0)) != EOF)
    {
      int group = opt >> USER_BITS;

      err = EINVAL;		/* until otherwise asserted */

      if (opt == 1)
	/* A non-option argument; try each parser in turn.  */
	for (o = options; o != NULL && err == EINVAL; o = o->parent)
	  err = (*o->parser)(opt, optarg);
      else if (group == 0)
	/* A short option.  */
	{
	  /* By comparing OPT's position in SHORT_OPTS to the various
	     starting positions in GROUP_SHORT_STARTS, we can determine which
	     group OPT came from.  */
	  char *short_index = index (short_opts, opt);
	  if (short_index)
	    for (o = options, group = 0; o != NULL; o = o->parent, group++)
	      if (o->parent == NULL
		  || group_short_starts[group + 1] > short_index)
		{
		  err = (*o->parser)(opt, optarg);
		  break;
		}
	}
      else
	/* A long option.  */
	for (o = options; o != NULL; o = o->parent)
	  if (--group == 0)
	    {
	      /* We use shifts instead of masking for extracting the user value
		 in order to preserve the sign.  */
	      err = (*o->parser)(((opt << GROUP_BITS) >> GROUP_BITS), optarg);
	      break;
	    }

      if (err)
	break;
    }

  if (arg_index != NULL)
    *arg_index = optind;

  mutex_unlock (&getopt_lock);

  return err;
}
