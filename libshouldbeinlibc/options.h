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

#ifndef __OPTIONS_H__
#define __OPTIONS_H__

#include <errno.h>
#include <getopt.h>

/* An options structure contains a set of getopt options declarations, a
   function to deal with getting one, and an optional pointer to another
   options structure.  When actually parsing options, getopt is called with
   the union of all the options structures chained together through their
   PARENT pointers, with conflicts being resolved in favor of the first
   occurance in the chain.  */
struct options
{
  /* The getopt-style short options string for this group of options.  */
  char *short_options;
  /* An array of getopt-style long-options structures.  */
  struct option *long_options;

  /* What to do with an option from this structure.  KEY is either the short
     option letter, or the final member of the long-option entry, as returned
     by getopt, and ARG is the value of OPTARG.  If a non-zero value is
     returned, then parsing is stopped immediately, and that value is
     returned from options_parse().  */
  error_t (*parser)(int key, char *arg);

  /* The next member in this options chain.  */
  struct options *parent;
};

/* Flags for options_parse:  */

/* Ignore the first element of ARGV.  Useful for program command lines.  */
#define OPTIONS_SKIP_ARG0    0x1

/* Print error messages for unknown options to stderr; if this flag is set,
   OPTIONS_SKIP_ARG0 is ignored, as ARGV[0] is used as the program name in
   the error messages.  */
#define OPTIONS_PRINT_ERRS   0x2

/* Parse non-option args as well, similarly to getopt, by calling the parse
   function with a key of 1, and the actual arg as the value.  Since it's
   impossible to know which parse function wants to handle it, each one is
   called in turn, until one returns 0 or an error other than EINVAL.  */
#define OPTIONS_PARSE_ARGS   0x4


/* Parse the options strings in ARGC & ARGV according to the options in
   OPTIONS.  FLAGS is one of the OPTIONS_ flags above.  If ARG_INDEX is
   non-NULL, the index in ARGV of the first unparsed option is returned in
   it.  If an unknown option is present, EINVAL is returned; if some parser
   routine returned a non-zero value, it is returned; otherwise 0 is
   returned.  */
error_t options_parse (struct options *options, int argc, char **argv,
		       unsigned flags, int *arg_index);

#endif /* __OPTIONS_H__ */
