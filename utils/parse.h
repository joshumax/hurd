/* Random helpful option parsing functions

   Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.

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

#ifndef __PARSE_H__
#define __PARSE_H__

#include <errno.h>
#include <argp.h>

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_FN isn't NULL, then call ADD_FN on the resutl of calling
   DEFAULT_FN instead, otherwise signal an error.  */
extern error_t
parse_strlist (char *arg,
	       error_t (*add_fn)(const char *str, struct argp_state *state),
	       const char *(*default_fn)(struct argp_state *state),
	       const char *type_name, struct argp_state *state);

/* For each numeric string in the comma-separated list in ARG, call ADD_FN;
   if ARG is empty and DEFAULT_FN isn't NULL, then call DEF_FN to get a number,
   and call ADD_FN on that, otherwise signal an error.  If any member of the
   list isn't a number, and LOOKUP_FN isn't NULL, then it is called to return
   an integer for the string.  LOOKUP_FN should signal an error itself it
   there's some problem parsing the string.  */
extern error_t
parse_numlist (char *arg,
	       error_t (*add_fn)(unsigned num, struct argp_state *state),
	       int (*default_fn)(struct argp_state *state),
	       int (*lookup_fn)(const char *str, struct argp_state *state),
	       const char *type_name, struct argp_state *state);

/* Return the index of which of a set of strings ARG matches, including
   non-ambiguous partial matches.  CHOICE_FN should be a function that
   returns the name of the Nth option, or 0 if N is out of range, and KIND
   should be a string that describes what's being matched, for use in error
   messages.  If ALLOW_MISMATCHES is true, then -1 is returned in the event
   that ARG matches no entry , otherwise, an error message is printed and the
   program exits in this event.  If ARG is an ambiguous match, an error
   message is printed and the program exits.  */
extern int
parse_enum (const char *arg,
	    const char *(*choice_fn)(unsigned n),
	    const char *kind, int allow_mismatches,
	    struct argp_state *state);

#endif /* __PARSE_H__ */
