/* Random helpful option parsing functions

   Copyright (C) 1995, 1996, 1997, 1998 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <error.h>

#include "parse.h"

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_ADD_FN isn't NULL, then call DEFAULT_ADD_FN instead. */
error_t
_parse_strlist (char *arg,
		error_t (*add_fn)(const char *str, struct argp_state *state),
		error_t (*default_add_fn)(struct argp_state *state),
		const char *type_name, struct argp_state *state)
{
  if (arg)
    while (isspace(*arg))
      arg++;

  if (arg == NULL || *arg == '\0')
    if (default_add_fn)
      return (*default_add_fn)(state);
    else
      {
	argp_error (state, "Empty %s list", type_name);
	return EINVAL;
      }
  else
    {
      error_t err = 0;
      char *end = arg;

      void mark_end()
	{
	  *end++ = '\0';
	  while (isspace(*end))
	    end++;
	}
      error_t parse_element()
	{
	  char *cur = arg;
	  if (*cur == '\0')
	    {
	      argp_error (state, "Empty element in %s list", type_name);
	      return EINVAL;
	    }
	  arg = end;
	  return (*add_fn)(cur, state);
	}

      while (*end != '\0' && !err)
	switch (*end)
	  {
	  case ' ': case '\t':
	    mark_end();
	    if (*end == ',')
	      mark_end();
	    err = parse_element();
	    break;
	  case ',':
	    mark_end();
	    err = parse_element();
	    break;
	  default:
	    end++;
	  }

      if (! err)
	err = parse_element();

      return err;
    }
}

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_FN isn't NULL, then call ADD_FN on the resutl of calling
   DEFAULT_FN instead, otherwise signal an error.  */
error_t
parse_strlist (char *arg,
	       error_t (*add_fn)(const char *str, struct argp_state *state),
	       const char *(*default_fn)(struct argp_state *state),
	       const char *type_name, struct argp_state *state)
{
  error_t default_str_add (struct argp_state *state)
    { return (*add_fn)((*default_fn)(state), state); }
  return _parse_strlist (arg, add_fn, default_str_add, type_name, state);
}

/* For each numeric string in the comma-separated list in ARG, call ADD_FN;
   if ARG is empty and DEFAULT_FN isn't NULL, then call DEF_FN to get a number,
   and call ADD_FN on that, otherwise signal an error.  If any member of the
   list isn't a number, and LOOKUP_FN isn't NULL, then it is called to return
   an integer for the string.  LOOKUP_FN should signal an error itself it
   there's some problem parsing the string.  */
error_t
parse_numlist (char *arg,
	       error_t (*add_fn)(unsigned num, struct argp_state *state),
	       int (*default_fn)(struct argp_state *state),
	       int (*lookup_fn)(const char *str, struct argp_state *state),
	       const char *type_name, struct argp_state *state)
{
  error_t default_num_add() { return (*add_fn)((*default_fn)(state), state); }
  error_t add_num_str(const char *str, struct argp_state *state)
    {
      const char *p;
      for (p = str; *p != '\0'; p++)
	if (!isdigit(*p))
	  {
	    if (lookup_fn)
	      return (*add_fn)((*lookup_fn)(str, state), state);
	    else
	      {
		argp_error (state, "%s: Invalid %s", p, type_name);
		return EINVAL;
	      }
	    return 0;
	  }
      return (*add_fn) (atoi (str), state);
    }
  return _parse_strlist(arg, add_num_str, default_fn ? default_num_add : 0,
			type_name, state);
}

/* Return the index of which of a set of strings ARG matches, including
   non-ambiguous partial matches.  CHOICE_FN should be a function that
   returns the name of the Nth option, or 0 if N is out of range, and KIND
   should be a string that describes what's being matched, for use in error
   messages.  If ALLOW_MISMATCHES is true, then -1 is returned in the event
   that ARG matches no entry , otherwise, an error message is printed and the
   program exits in this event.  If ARG is an ambiguous match, an error
   message is printed and the program exits.  */
int
parse_enum (const char *arg,
	    const char *(*choice_fn)(unsigned n),
            const char *kind, int allow_mismatches,
	    struct argp_state *state)
{
  const char *choice;
  int arglen = strlen (arg);
  int n = 0;
  int partial_match = -1;

  while ((choice = (*choice_fn)(n)) != NULL)
    if (strcasecmp (choice, arg) == 0)
      return n;
    else
      {
	if (strncasecmp (choice, arg, arglen) == 0)
	  {
	    if (partial_match >= 0)
	      {
		argp_error (state, "%s: Ambiguous %s", arg, kind);
		return -1;
	      }
	    else
	      partial_match = n;
	  }
	n++;
      }

  if (partial_match < 0 && !allow_mismatches)
    argp_error (state, "%s: Invalid %s", arg, kind);

  return partial_match;
}
