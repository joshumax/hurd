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

#ifndef __ARGP_H__
#define __ARGP_H__

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <getopt.h>

/* A description of a particular option.  A pointer to an array of
   these is passed in the OPTIONS field of an argp structure.  Each option
   entry can correspond to one long option and/or one short option; more
   names for the same option can be added by following an entry in an option
   array with options having the OPTION_ALIAS flag set.  */
struct argp_option
{
  /* The long option name.  For more than one name for the same option, you
     can use following options with the OPTION_ALIAS flag set.  */
  const char *name;

  /* What key is returned for this option.  If > 0 and printable, then it's
     also accepted as a short option.  */
  int key;

  /* If non-NULL, this is the name of the argument associated with this
     option, which is required unless the OPTION_ARG_OPTIONAL flag is set. */
  const char *arg;

  /* OPTION_ flags.  */
  int flags;

  /* The doc string for this option.  If both NAME and KEY are 0, This string
     will be printed outdented from the normal option column, making it
     useful as a group header (it will be the first thing printed in its
     group); in this usage, it's conventional to end the string with a `:'.  */
  const char *doc;

  /* The group this option is in.  In a long help message, options are sorted
     alphabetically within each group, and the groups presented in the order
     1, 2, ..., n, 0, -m, ..., -2, -1.  Every entry in an options array with
     if this field 0 will inherit the group number of the previous entry, or
     zero if it's the first one.  Automagic options such as --help are put
     into group -1.  */
  int group;
};

/* The argument associated with this option is optional.  */
#define OPTION_ARG_OPTIONAL	0x1
/* This option isn't displayed in any help messages.  */
#define OPTION_HIDDEN	       	0x2
/* This option is an alias for the closest previous non-alias option.  This
   means that it will be displayed in the same help entry, and will inherit
   fields other than NAME and KEY from the aliased option.  */
#define OPTION_ALIAS		0x4

struct argp;			/* fwd declare this type */
struct argp_state;		/* " */

/* The type of a pointer to an argp parsing function.  */
typedef error_t (*argp_parser_t)(int key, char *arg, struct argp_state *state);

/* What to return for unrecognized keys.  For special ARGP_KEY_ keys, such
   returns will simply be ignored.  For user keys, this error will be turned
   into EINVAL (if the call to argp_parse is such that errors are propagated
   back to the user instead of exiting); returning EINVAL itself would result
   in an immediate stop to parsing in *all* cases.  */
#define ARGP_ERR_UNKNOWN	E2BIG /* Hurd should never need E2BIG.  XXX */

/* Special values for the KEY argument to an argument parsing function.
   ARGP_ERR_UNKNOWN should be returned if they aren't understood.  

   The sequence of keys to parser calls is either (where opt is a user key):
       ARGP_KEY_INIT (opt | ARGP_KEY_ARG)... ARGP_KEY_END
   or  ARGP_KEY_INIT opt... ARGP_KEY_NO_ARGS ARGP_KEY_END

   If an error occurs, then the parser is called with ARGP_KEY_ERR, and no
   other calls are made.  */

/* This is not an option at all, but rather a command line argument.  If a
   parser receiving this key returns success, the fact is recorded, and the
   ARGP_KEY_NO_ARGS case won't be used.  HOWEVER, if while processing the
   argument, a parser function decrements the NEXT field of the state it's
   passed, the option won't be considered processed; this is to allow you to
   actually modify the argument (perhaps into an option), and have it
   processed again.  */
#define ARGP_KEY_ARG		0
/* There are no more command line arguments at all.  */
#define ARGP_KEY_END		1
/* Because it's common to want to do some special processing if there aren't
   any non-option args, user parsers are called with this key if they didn't
   successfully process any non-option arguments.  Called just before
   ARGP_KEY_END (where more general validity checks on previously parsed
   arguments can take place).  */
#define ARGP_KEY_NO_ARGS	2
/* Passed in before any parsing is done.  Afterwards, the values of each
   element of the CHILD_INPUT field, if any, in the state structure is
   copied to each child's state to be the initial value of the INPUT field.  */
#define ARGP_KEY_INIT		3
/* Passed in when parsing has successfully been completed (even if there are
   still arguments remaining).  */
#define ARGP_KEY_SUCCESS	4
/* Passed in if an error occurs (in which case a call with ARGP_KEY_SUCCESS is
   never made, so any cleanup must be done here).  */
#define ARGP_KEY_ERROR		5

/* An argp structure contains a set of getopt options declarations, a
   function to deal with getting one, and an optional pointer to another
   argp structure.  When actually parsing options, getopt is called with
   the union of all the argp structures chained together through their
   CHILD pointers, with conflicts being resolved in favor of the first
   occurance in the chain.  */
struct argp
{
  /* An array of argp_option structures, terminated by an entry with both
     NAME and KEY having a value of 0.  */
  const struct argp_option *options;

  /* What to do with an option from this structure.  KEY is the key
     associated with the option, and ARG is any associated argument (NULL if
     none was supplied).  If KEY isn't understood, ARGP_ERR_UNKNOWN should be
     returned.  If a non-zero, non-ARGP_ERR_UNKNOWN value is returned, then
     parsing is stopped immediately, and that value is returned from
     argp_parse().  For special (non-user-supplied) values of KEY, see the
     ARGP_KEY_ definitions below.  */
  argp_parser_t parser;

  /* A string describing what other arguments are wanted by this program.  It
     is only used by argp_usage to print the `Usage:' message.  */
  const char *args_doc;

  /* A string containing extra text to be printed after the options in a long
     help message, if it is non-NULL.  */
  const char *doc;

  /* A NULL terminated list of other argp structures that should be parsed
     with this one.  Any conflicts are resolved in favor of this argp, or
     early argps in the CHILDREN list.  This field is useful if you use
     libraries that supply their own argp structure, which you want to use in
     conjunction with your own.  */
  const struct argp **children;
};

/* Parsing state.  This is provided to parsing functions called by argp,
   which may examine and, as noted, modify fields.  */
struct argp_state
{
  /* The top level ARGP being parsed.  */
  const struct argp *argp;

  /* The argument vector being parsed.  May be modified.  */
  int argc;
  char **argv;

  /* The index in ARGV of the next arg that to be parsed.  May be modified. */
  int next;

  /* The flags supplied to argp_parse.  May be modified.  */
  unsigned flags;

  /* While calling a parsing function with a key of ARGP_KEY_ARG, this is the
     number of the current arg, starting at zero, and incremented after each
     such call returns.  At all other times, this is the number of such
     arguments that have been processed.  */
  unsigned arg_num;

  /* If non-zero, the index in ARGV of the first argument following a special
     `--' argument (which prevents anything following being interpreted as an
     option).  Only set once argument parsing has proceeded past this point. */
  int quoted;

  /* An arbitrary pointer passed in from the user.  */
  void *input;
  /* Values to pass to child parsers.  This vector will be the same length as
     the number of children for the current parser.  */
  void **child_inputs;

  /* For the parser's use.  Initialized to 0.  */
  void *hook;
};

/* Flags for argp_parse (note that the defaults are those that are
   convenient for program command line parsing): */

/* Don't ignore the first element of ARGV.  Normally (and always unless
   ARGP_NO_ERRS is set) the first element of the argument vector is
   skipped for option parsing purposes, as it corresponds to the program name
   in a command line.  */
#define ARGP_PARSE_ARGV0    0x1

/* Don't print error messages for unknown options to stderr; unless this flag
   is set, ARGP_PARSE_ARGV0 is ignored, as ARGV[0] is used as the program
   name in the error messages.  This flag implies ARGP_NO_EXIT (on the
   assumption that silent exiting upon errors is bad behaviour).  */
#define ARGP_NO_ERRS   0x2

/* Don't parse any non-option args.  Normally non-option args are parsed by
   calling the parse functions with a key of ARGP_KEY_ARG, and the actual arg
   as the value.  Since it's impossible to know which parse function wants to
   handle it, each one is called in turn, until one returns 0 or an error
   other than ARGP_ERR_UNKNOWN; if an argument is handled by no one, the
   argp_parse returns prematurely (but with a return value of 0).  If all
   args have been parsed without error, all parsing functions are called one
   last time with a key of ARGP_KEY_END.  This flag needn't normally be set,
   as the normal behavior is to stop parsing as soon as some argument can't
   be handled.  */
#define ARGP_NO_ARGS   0x4

/* Parse options and arguments in the same order they occur on the command
   line -- normally they're rearranged so that all options come first. */
#define ARGP_IN_ORDER  0x8

/* Don't provide the standard long option --help, which causes usage and
      option help information to be output to stdout, and exit (0) called. */
#define ARGP_NO_HELP   0x16

/* Don't exit on errors (they may still result in error messages).  */
#define ARGP_NO_EXIT   0x32

/* Turns off any message-printing/exiting options.  */
#define ARGP_SILENT    (ARGP_NO_EXIT | ARGP_NO_ERRS | ARGP_NO_HELP)

/* Parse the options strings in ARGC & ARGV according to the options in ARGP.
   FLAGS is one of the ARGP_ flags above.  If ARG_INDEX is non-NULL, the
   index in ARGV of the first unparsed option is returned in it.  If an
   unknown option is present, ARGP_ERR_UNKNOWN is returned; if some parser
   routine returned a non-zero value, it is returned; otherwise 0 is
   returned.  This function may also call exit unless the ARGP_NO_HELP flag
   is set.  INPUT is a pointer to a value to be passed in to the parser.  */
error_t argp_parse (const struct argp *argp,
		    int argc, char **argv, unsigned flags,
		    int *arg_index, void *input);

/* If defined or set by the user program to a non-zero value, then a default
   option --version is added (unless the ARGP_NO_HELP flag is used), which
   will print this this string and exit (unless the ARGP_NO_EXIT flag is
   used).  Overridden by ARGP_PROGRAM_VERSION_HOOK.  */
extern char *argp_program_version;

/* If defined or set by the user program to a non-zero value, then a default
   option --version is added (unless the ARGP_NO_HELP flag is used), which
   calls this function and exits (unless the ARGP_NO_EXIT flag is used).
   This variable takes precedent over ARGP_PROGRAM_VERSION.  */
extern void (*argp_program_version_hook) ();

/* Flags for argp_help.  */
#define ARGP_HELP_USAGE		0x01 /* Print a Usage: message. */
#define ARGP_HELP_SHORT_USAGE	0x02 /*  " but don't actually print options. */
#define ARGP_HELP_SEE		0x04 /* Print a `for more help...' message. */
#define ARGP_HELP_LONG		0x08 /* Print a long help message. */

/* These ARGP_HELP flags are only understood by argp_state_help.  */
#define ARGP_HELP_EXIT_ERR	0x10 /* Call exit(1) instead of returning.  */
#define ARGP_HELP_EXIT_OK	0x20 /* Call exit(0) instead of returning.  */

/* The standard thing to do after a program command line parsing error, if an
   error messages has already been printed.  */
#define ARGP_HELP_STD_ERR \
  (ARGP_HELP_SEE | ARGP_HELP_EXIT_ERR)
/* The standard thing to do after a program command line parsing error, if no
   more specific error message has been printed.  */
#define ARGP_HELP_STD_USAGE \
  (ARGP_HELP_SHORT_USAGE | ARGP_HELP_SEE | ARGP_HELP_EXIT_ERR)
/* The standard thing to do in response to a --help option.  */
#define ARGP_HELP_STD_HELP \
  (ARGP_HELP_SHORT_USAGE | ARGP_HELP_LONG | ARGP_HELP_EXIT_OK)

/* Output a usage message for ARGP to STREAM.  FLAGS are from the set
   ARGP_HELP_*.  */
extern void argp_help (const struct argp *argp, FILE *stream, unsigned flags);

/* The following routines are intended to be called from within an argp
   parsing routine (thus taking an argp_state structure as the first
   argument).  They may or may not print an error message and exit, depending
   on the flags in STATE -- in any case, the caller should be prepared for
   them *not* to exit, and should return an appropiate error after calling
   them.  [argp_usage & argp_error should probably be called argp_state_...,
   but they're used often enough that they should be short]  */

/* Output, if appropriate, a usage message for STATE to STREAM.  FLAGS are
   from the set ARGP_HELP_*.  */
void argp_state_help (struct argp_state *state, FILE *stream, unsigned flags);

/* Possibly output the standard usage message for ARGP to stderr and exit.  */
extern inline void
argp_usage (struct argp_state *state)
{
  argp_state_help (state, stderr, ARGP_HELP_STD_USAGE);
}

/* If appropriate, print the printf string FMT and following args, preceded
   by the program name and `:', to stderr, and followed by a `Try ... --help'
   message, then exit (1).  */
void argp_error (struct argp_state *state, const char *fmt, ...)
     __attribute__ ((format (printf, 2, 3)));

/* Returns true if the option OPT is a valid short option.  */
extern inline int
_option_is_short (const struct argp_option *opt)
{
  int key = opt->key;
  return key > 0 && isprint (key);
}

/* Returns true if the option OPT is in fact the last (unused) entry in an
   options array.  */
extern inline int
_option_is_end (const struct argp_option *opt)
{
  return !opt->key && !opt->name && !opt->doc && !opt->group;
}

#endif /* __ARGP_H__ */
