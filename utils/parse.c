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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <error.h>

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_ADD_FN isn't NULL, then call DEFAULT_ADD_FN instead. */
void
_parse_strlist (char *arg,
		void (*add_fn)(const char *str), void (*default_add_fn)(),
		const char *type_name)
{
  if (arg)
    while (isspace(*arg))
      arg++;

  if (arg == NULL || *arg == '\0')
    if (default_add_fn)
      (*default_add_fn)();
    else
      error(7, 0, "Empty %s list", type_name);
  else
    {
      char *end = arg;

      void mark_end()
	{
	  *end++ = '\0';
	  while (isspace(*end))
	    end++;
	}
      void parse_element()
	{
	  if (*arg == '\0')
	    error(7, 0, "Empty element in %s list", type_name);
	  (*add_fn)(arg);
	  arg = end;
	}

      while (*end != '\0')
	switch (*end)
	  {
	  case ' ': case '\t':
	    mark_end();
	    if (*end == ',')
	      mark_end();
	    parse_element();
	    break;
	  case ',':
	    mark_end();
	    parse_element();
	    break;
	  default:
	    end++;
	  }

      parse_element();
    }
}

/* For each string in the comma-separated list in ARG, call ADD_FN; if ARG is
   empty and DEFAULT_FN isn't NULL, then call ADD_FN on the resutl of calling
   DEFAULT_FN instead, otherwise signal an error.  */
void
parse_strlist (char *arg,
	       void (*add_fn)(const char *str),
	       const char *(*default_fn)(),
	       const char *type_name)
{
  void default_str_add() { (*add_fn)((*default_fn)()); }
  _parse_strlist(arg, add_fn, default_str_add, type_name);
}

/* For each numeric string in the comma-separated list in ARG, call ADD_FN;
   if ARG is empty and DEFAULT_FN isn't NULL, then call DEF_FN to get a number,
   and call ADD_FN on that, otherwise signal an error.  If any member of the
   list isn't a number, and LOOKUP_FN isn't NULL, then it is called to return
   an integer for the string.  LOOKUP_FN should signal an error itself it
   there's some problem parsing the string.  */
void
parse_numlist (char *arg,
	       void (*add_fn)(unsigned num),
	       int (*default_fn)(),
	       int (*lookup_fn)(const char *str),
	       const char *type_name)
{
  void default_num_add() { (*add_fn)((*default_fn)()); }
  void add_num_str(const char *str)
    {
      const char *p;
      for (p = str; *p != '\0'; p++)
	if (!isdigit(*p))
	  {
	    if (lookup_fn)
	      (*add_fn)((*lookup_fn)(str));
	    else
	      error (7, 0, "%s: Invalid %s", p, type_name);
	    return;
	  }
      (*add_fn)(atoi(str));
    }
  _parse_strlist(arg, add_num_str, default_fn ? default_num_add : 0,
		 type_name);
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
parse_enum (char *arg, const char *(*choice_fn)(unsigned n),
            char *kind, int allow_mismatches)
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
	  if (partial_match >= 0)
	    error (7, 0, "%s: Ambiguous %s", arg, kind);
	  else
	    partial_match = n;
	n++;
      }

  if (partial_match < 0 && !allow_mismatches)
    error (7, 0, "%s: Invalid %s", arg, kind);

  return partial_match;
}
