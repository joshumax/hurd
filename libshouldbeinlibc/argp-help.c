/* Hierarchial argument parsing help output

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
#include <assert.h>
#include <stdarg.h>
#include <malloc.h>
#include <ctype.h>
#include <linewrap.h>

#include "argp.h"

#define SHORT_OPT_COL 2		/* column in which short options start */
#define LONG_OPT_COL  6		/* column in which long options start */
#define OPT_DOC_COL  29		/* column in which option text starts */
#define HEADER_COL    1		/* column in which group headers are printed */
#define USAGE_INDENT 12		/* indentation of wrapped usage lines */
#define RMARGIN      79		/* right margin used for wrapping */

/* Returns true if OPT hasn't been marked invisible.  Visibility only affects
   whether OPT is displayed or used in sorting, not option shadowing.  */
#define ovisible(opt) (! ((opt)->flags & OPTION_HIDDEN))

/* Returns true if OPT is an alias for an earlier option.  */
#define oalias(opt) ((opt)->flags & OPTION_ALIAS)

/* Returns true if OPT is the end-of-list marker for a list of options.  */
#define oend(opt) _option_is_end (opt)

/* Returns true if OPT has a short option.  */
#define oshort(opt) _option_is_short (opt)

/*
   The help format for a particular option is like:

     -xARG, -yARG, --long1=ARG, --long2=ARG        Documentation...

   Where ARG will be omitted if there's no argument, for this option, or
   will be surrounded by "[" and "]" appropiately if the argument is
   optional.  The documentation string is word-wrapped appropiately, and if
   the list of options is long enough, it will be started on a separate line.
   If there are no short options for a given option, the first long option is
   indented slighly in a way that's supposed to make most long options appear
   to be in a separate column.

   For example (from ps): 

  -p PID, --pid=PID           List the process PID
      --pgrp=PGRP            List processes in the process group PGRP
  -P, -x, --no-parent        Include processes without parents
  -Q, --all-fields           Don't elide unusable fields (normally if there's
                             some reason ps can't print a field for any
                             process, it's removed from the output entirely)
  -r, --reverse, --gratuitously-long-reverse-option
                             Reverse the order of any sort
      --session[=SID]        Add the processes from the session SID (which
                             defaults to the sid of the current process)

   The struct argp_option array for the above could look like:

   {
     {"pid",       'p',      "PID",  0,
	"List the process PID"},
     {"pgrp",      OPT_PGRP, "PGRP", 0,
	"List processes in the process group PGRP"},
     {"no-parent", 'P',	      0,     0,
	"Include processes without parents"},
     {0,           'x',       0,     OPTION_ALIAS},
     {"all-fields",'Q',       0,     0,
	"Don't elide unusable fields (normally if there's some reason ps \
can't print a field for any process, it's removed from the output entirely)"},
     {"reverse",   'r',       0,     0,
	"Reverse the order of any sort"},
     {"gratuitously-long-reverse-option", 0, 0, OPTION_ALIAS},
     {"session",   OPT_SESS,  "SID", OPTION_ARG_OPTIONAL,
	"Add the processes from the session SID (which defaults to the sid of \
the current process)"},
   }

*/

/* Returns true if CH occurs between BEG and END.  */
static int
find_char (char ch, char *beg, char *end)
{
  while (beg < end)
    if (*beg == ch)
      return 1;
    else
      beg++;
  return 0;
}

struct hol_entry
{
  /* First option.  */
  const struct argp_option *opt;
  /* Number of options (including aliases).  */
  unsigned num;

  /* A pointers into the HOL's short_options field, to the first short option
     letter for this entry.  The order of the characters following this point
     corresponds to the order of options pointed to by OPT, and there are at
     most NUM.  A short option recorded in a option following OPT is only
     valid if it occurs in the right place in SHORT_OPTIONS (otherwise it's
     probably been shadowed by some other entry).  */
  char *short_options;

  /* Entries are sorted by their group first, in the order:
       1, 2, ..., n, 0, -m, ..., -2, -1
     and then alphabetically within each group.  The default is 0.  */
  int group;
};

/* A list of options for help.  */
struct hol
{
  /* The number of entries in this hol.  If this field is zero, the others
     are undefined.  */
  unsigned num_entries;
  /* An array of hol_entry's.  */
  struct hol_entry *entries;
  /* A string containing all short options in this HOL.  Each entry contains
     pointers into this string, so the order can't be messed with blindly.  */
  char *short_options;
};

/* Create a struct hol from an array of struct argp_option.  */
struct hol *make_hol (const struct argp_option *opt)
{
  char *so;
  const struct argp_option *o;
  struct hol_entry *entry;
  unsigned num_short_options = 0;
  struct hol *hol = malloc (sizeof (struct hol));

  assert (hol);

  hol->num_entries = 0;

  if (opt)
    {
      int cur_group = 0;

      /* The first option must not be an alias.  */
      assert (! oalias (opt));

      /* Calculate the space needed.  */
      for (o = opt; ! oend (o); o++)
	{
	  if (! oalias (o))
	    hol->num_entries++;
	  if (oshort (o))
	    num_short_options++;	/* This is an upper bound.  */
	}

      hol->entries = malloc (sizeof (struct hol_entry) * hol->num_entries);
      hol->short_options = malloc (num_short_options + 1);

      assert (hol->entries && hol->short_options);

      /* Fill in the entries.  */
      so = hol->short_options;
      for (o = opt, entry = hol->entries; ! oend (o); entry++)
	{
	  entry->opt = o;
	  entry->num = 0;
	  entry->short_options = so;
	  entry->group = cur_group = o->group ?: cur_group;

	  do
	    {
	      entry->num++;
	      if (oshort (o) && ! find_char (o->key, hol->short_options, so))
		/* O has a valid short option which hasn't already been used.*/
		*so++ = o->key;
	      o++;
	    }
	  while (! oend (o) && oalias (o));
	}
      *so = '\0';		/* null terminated so we can find the length */
    }

  return hol;
}

/* Free HOL and any resources it uses.  */
static void
hol_free (struct hol *hol)
{
  if (hol->num_entries > 0)
    {
      free (hol->entries);
      free (hol->short_options);
    }
  free (hol);
}

static inline int
hol_entry_short_iterate (const struct hol_entry *entry,
			 int (*func)(const struct argp_option *opt,
				     const struct argp_option *real))
{
  unsigned nopts;
  int val = 0;
  const struct argp_option *opt, *real = entry->opt;
  char *so = entry->short_options;

  for (opt = real, nopts = entry->num; nopts > 0 && !val; opt++, nopts--)
    if (oshort (opt) && *so == opt->key)
      {
	if (!oalias (opt))
	  real = opt;
	if (ovisible (opt))
	  val = (*func)(opt, real);
	so++;
      }

  return val;
}

static inline int
hol_entry_long_iterate (const struct hol_entry *entry,
			int (*func)(const struct argp_option *opt,
				    const struct argp_option *real))
{
  unsigned nopts;
  int val = 0;
  const struct argp_option *opt, *real = entry->opt;

  for (opt = real, nopts = entry->num; nopts > 0 && !val; opt++, nopts--)
    if (opt->name)
      {
	if (!oalias (opt))
	  real = opt;
	if (ovisible (opt))
	  val = (*func)(opt, real);
      }

  return val;
}

/* Returns the first valid short option in ENTRY, or 0 if there is none.  */
static char
hol_entry_first_short (const struct hol_entry *entry)
{
  inline int func1 (const struct argp_option *opt,
		    const struct argp_option *real)
    {
      return opt->key;
    }
  return hol_entry_short_iterate (entry, func1);
}

/* Returns the first valid long option in ENTRY, or 0 if there is none.  */
static const char *
hol_entry_first_long (const struct hol_entry *entry)
{
  const struct argp_option *opt;
  unsigned num;
  for (opt = entry->opt, num = entry->num; num > 0; opt++, num--)
    if (opt->name && ovisible (opt))
      return opt->name;
  return 0;
}

/* Returns the entry in HOL with the long option name NAME, or 0 if there is
   none.  */
static struct hol_entry *hol_find_entry (struct hol *hol, char *name)
{
  struct hol_entry *entry = hol->entries;
  unsigned num_entries = hol->num_entries;

  while (num_entries-- > 0)
    {
      const struct argp_option *opt = entry->opt;
      unsigned num_opts = entry->num;

      while (num_opts-- > 0)
	if (opt->name && ovisible (opt) && strcmp (opt->name, name) == 0)
	  return entry;
	else
	  opt++;

      entry++;
    }

  return 0;
}

/* If an entry with the long option NAME occurs in HOL, set it's special
   sort position to GROUP.  */
static void
hol_set_group (struct hol *hol, char *name, int group)
{
  struct hol_entry *entry = hol_find_entry (hol, name);
  if (entry)
    entry->group = group;
}

/* Sort HOL by group and alphabetically by option name (with short options
   taking precedence over long).  Since the sorting is for display purposes
   only, the shadowing of options isn't effected.  */
static void
hol_sort (struct hol *hol)
{
  int entry_cmp (const void *entry1_v, const void *entry2_v)
    {
      const struct hol_entry *entry1 = entry1_v, *entry2 = entry2_v;
      int group1 = entry1->group, group2 = entry2->group;

      if (group1 == group2)
	/* Normal comparison.  */
	{
	  int short1 = hol_entry_first_short (entry1);
	  int short2 = hol_entry_first_short (entry2);
	  const char *long1 = hol_entry_first_long (entry1);
	  const char *long2 = hol_entry_first_long (entry2);

	  if (!short1 && !short2 && long1 && long2)
	    /* Only long options.  */
	    return strcasecmp (long1, long2);
	  else
	    /* Compare short/short, long/short, short/long, using the first
	       character of long options.  Entries without *any* valid
	       options (such as options with OPTION_HIDDEN set) will be put
	       first, but as they're not displayed, it doesn't matter where
	       they are.  */
	    {
	      char first1 = short1 ?: long1 ? *long1 : 0;
	      char first2 = short2 ?: long2 ? *long2 : 0;
	      /* Compare ignoring case, except when the options are both the
		 same letter, in which case lower-case always comes first.  */
	      return (tolower (first1) - tolower (first2)) ?: first2 - first1;
	    }
	}
      else
	/* Order by group:  1, 2, ..., n, 0, -m, ..., -2, -1  */
	if ((group1 < 0 && group2 < 0) || (group1 > 0 && group2 > 0))
	  return group1 - group2;
	else
	  return group2 - group1;
    }

  if (hol->num_entries > 0)
    qsort (hol->entries, hol->num_entries, sizeof (struct hol_entry),
	   entry_cmp);
}

/* Append MORE to HOL, destroying MORE in the process.  Options in HOL shadow
   any in MORE with the same name.  */
static void
hol_append (struct hol *hol, struct hol *more)
{
  if (more->num_entries == 0)
    hol_free (more);
  else if (hol->num_entries == 0)
    {
      hol->num_entries = more->num_entries;
      hol->entries = more->entries;
      hol->short_options = more->short_options;
      /* We've stolen everything MORE from more.  Destroy the empty shell. */
      free (more);		
    }
  else
    /* append the entries in MORE to those in HOL, taking care to only add
       non-shadowed SHORT_OPTIONS values.  */
    {
      unsigned left;
      char *so, *more_so;
      struct hol_entry *e;
      unsigned num_entries = hol->num_entries + more->num_entries;
      struct hol_entry *entries =
	malloc (num_entries * sizeof (struct hol_entry));
      unsigned hol_so_len = strlen (hol->short_options);
      char *short_options =
	malloc (hol_so_len + strlen (more->short_options) + 1);

      bcopy (hol->entries, entries,
	     hol->num_entries * sizeof (struct hol_entry));
      bcopy (more->entries, entries + hol->num_entries,
	     more->num_entries * sizeof (struct hol_entry));

      bcopy (hol->short_options, short_options, hol_so_len);

      /* Fix up the short options pointers from HOL.  */
      for (e = entries, left = hol->num_entries; left > 0; e++, left--)
	e->short_options += (short_options - hol->short_options);

      /* Now add the short options from MORE, fixing up its entries too.  */
      so = short_options + hol_so_len;
      more_so = more->short_options;
      for (left = more->num_entries; left > 0; e++, left--)
	{
	  int opts_left;
	  const struct argp_option *opt;

	  e->short_options = so;

	  for (opts_left = e->num, opt = e->opt; opts_left; opt++, opts_left--)
	    {
	      int ch = *more_so;
	      if (oshort (opt) && ch == opt->key)
		/* The next short option in MORE_SO, CH, is from OPT.  */
		{
		  if (! find_char (ch,
				   short_options, short_options + hol_so_len))
		    /* The short option CH isn't shadowed by HOL's options,
		       so add it to the sum.  */
		    *so++ = ch;
		  more_so++;
		}
	    }
	}

      *so = '\0';

      free (hol->entries);
      free (hol->short_options);

      hol->entries = entries;
      hol->num_entries = num_entries;
      hol->short_options = short_options;

      hol_free (more);
    }
}

/* Inserts enough spaces to make sure STREAM is at column COL.  */
static void
indent_to (FILE *stream, unsigned col)
{
  int needed = col - line_wrap_point (stream);
  while (needed-- > 0)
    putc (' ', stream);
}

/* Print help for ENTRY to STREAM.  *LAST_ENTRY should contain the last entry
   printed before this, or null if it's the first, and if ENTRY is in a
   different group, and *SEP_GROUPS is true, then a blank line will be
   printed before any output.  *SEP_GROUPS is also set to true if a
   user-specified group header is printed.  */
static void
hol_entry_help (struct hol_entry *entry, FILE *stream,
		struct hol_entry **prev_entry, int *sep_groups)
{
  unsigned num;
  int first = 1;		/* True if nothing's been printed so far.  */
  const struct argp_option *real = entry->opt, *opt;
  char *so = entry->short_options;
  int old_lm = line_wrap_set_lmargin (stream, 0);
  int old_wm = line_wrap_set_wmargin (stream, 0);

  /* Inserts a comma if this isn't the first item on the line, and then makes
     sure we're at least to column COL.  Also clears FIRST.  */
  void comma (unsigned col)
    {
      if (first)
	{
	  if (sep_groups && *sep_groups
	      && prev_entry && *prev_entry
	      && entry->group != (*prev_entry)->group)
	    putc ('\n', stream);
	  first = 0;
	}
      else
	fputs (", ", stream);
      indent_to (stream, col);
    }

  /* If the option REAL has an argument, we print it in using the printf
     format REQ_FMT or OPT_FMT depending on whether it's a required or
     optional argument.  */
  void arg (char *req_fmt, char *opt_fmt)
    {
      if (real->arg)
	if (real->flags & OPTION_ARG_OPTIONAL)
	  fprintf (stream, opt_fmt, real->arg);
	else
	  fprintf (stream, req_fmt, real->arg);
    }

  /* First emit short options.  */
  line_wrap_set_wmargin (stream, SHORT_OPT_COL); /* For truly bizarre cases. */
  for (opt = real, num = entry->num; num > 0; opt++, num--)
    if (oshort (opt) && opt->key == *so)
      /* OPT has a valid (non shadowed) short option.  */
      {
	if (ovisible (opt))
	  {
	    comma (SHORT_OPT_COL);
	    putc ('-', stream);
	    putc (*so, stream);
	    arg (" %s", "[%s]");
	  }
	so++;
      }

  /* Now, long options.  */
  line_wrap_set_wmargin (stream, LONG_OPT_COL);
  for (opt = real, num = entry->num; num > 0; opt++, num--)
    if (opt->name && ovisible (opt))
      {
	comma (LONG_OPT_COL);
	fprintf (stream, "--%s", opt->name);
	arg ("=%s", "[=%s]");
      }

  line_wrap_set_lmargin (stream, 0);
  if (first)
    /* Didn't print any switches, what's up?  */
    if (!oshort (real) && !real->name && real->doc)
      /* This is a group header, print it nicely.  */
      {
	if (*real->doc)
	  {
	    if (prev_entry && *prev_entry)
	      putc ('\n', stream); /* Precede with a blank line.  */
	    indent_to (stream, HEADER_COL);
	    line_wrap_set_lmargin (stream, HEADER_COL);
	    line_wrap_set_wmargin (stream, HEADER_COL);
	    fputs (real->doc, stream);
	  }
	if (sep_groups)
	  *sep_groups = 1;	/* Separate subsequent groups. */
      }
    else
      /* Just a totally shadowed option or null header; print nothing.  */
      return;			
  else if (real->doc)
    /* Now the option documentation.  */
    {
      unsigned col = line_wrap_point (stream);
      const char *doc = real->doc;

      line_wrap_set_lmargin (stream, OPT_DOC_COL);
      line_wrap_set_wmargin (stream, OPT_DOC_COL);

      if (col > OPT_DOC_COL + 3)
	putc ('\n', stream);
      else if (col >= OPT_DOC_COL)
	fprintf (stream, "   ");
      else
	indent_to (stream, OPT_DOC_COL);

      fputs (doc, stream);
    }

  line_wrap_set_lmargin (stream, 0); /* Don't follow the nl with spaces. */
  putc ('\n', stream);
  line_wrap_set_lmargin (stream, old_lm);
  line_wrap_set_wmargin (stream, old_wm);

  if (prev_entry)
    *prev_entry = entry;
}

/* Output a long help message about the options in HOL to STREAM.  */
static void
hol_help (struct hol *hol, FILE *stream)
{
  unsigned num;
  struct hol_entry *entry;
  struct hol_entry *last_entry = 0;
  int sep_groups = 0;		/* True if we should separate different
				   sections with blank lines.   */
  for (entry = hol->entries, num = hol->num_entries; num > 0; entry++, num--)
    hol_entry_help (entry, stream, &last_entry, &sep_groups);
}

/* Print a short usage description for the arguments in HOL to STREAM.  */
static void
hol_usage (struct hol *hol, FILE *stream)
{
  if (hol->num_entries > 0)
    {
      unsigned nentries;
      struct hol_entry *entry;
      char *short_no_arg_opts = alloca (strlen (hol->short_options));
      char *snao_end = short_no_arg_opts;

      /* First we put a list of short options without arguments.  */
      for (entry = hol->entries, nentries = hol->num_entries
	   ; nentries > 0
	   ; entry++, nentries--)
	{
	  inline int func2 (const struct argp_option *opt,
			    const struct argp_option *real)
	    {
	      if (! (opt->arg || real->arg))
		*snao_end++ = opt->key;
	      return 0;
	    }
	  hol_entry_short_iterate (entry, func2);
	}
      if (snao_end > short_no_arg_opts)
	{
	  *snao_end++ = 0;
	  fprintf (stream, " [-%s]", short_no_arg_opts);
	}

      /* Now a list of short options *with* arguments.  */
      for (entry = hol->entries, nentries = hol->num_entries
	   ; nentries > 0
	   ; entry++, nentries--)
	{
	  inline int func3 (const struct argp_option *opt,
			    const struct argp_option *real)
	    {
	      if (opt->arg || real->arg)
		if ((opt->flags | real->flags) & OPTION_ARG_OPTIONAL)
		  fprintf (stream, " [-%c[%s]]",
			   opt->key, opt->arg ?: real->arg);
		else
		  {
		    const char *arg = opt->arg ?: real->arg;
		    /* Manually do line wrapping so that it (probably) won't
		       get wrapped at the embedded space.  */
		    if (line_wrap_point (stream) + 6 + strlen (arg)
			>= line_wrap_rmargin (stream))
		      putc ('\n', stream);
		    else
		      putc (' ', stream);
		    fprintf (stream, "[-%c %s]", opt->key, arg);
		  }
	      return 0;
	    }
	  hol_entry_short_iterate (entry, func3);
	}

      /* Finally, a list of long options (whew!).  */
      for (entry = hol->entries, nentries = hol->num_entries
	   ; nentries > 0
	   ; entry++, nentries--)
	{
	  int func4 (const struct argp_option *opt,
		     const struct argp_option *real)
	    {
	      if (opt->arg || real->arg)
		if ((opt->flags | real->flags) & OPTION_ARG_OPTIONAL)
		  fprintf (stream, " [--%s[=%s]]",
			   opt->name, opt->arg ?: real->arg);
		else
		  fprintf (stream, " [--%s=%s]",
			   opt->name, opt->arg ?: real->arg);
	      else
		fprintf (stream, " [--%s]", opt->name);
	      return 0;
	    }
	  hol_entry_long_iterate (entry, func4);
	}
    }
}

/* Make a HOL containing all levels of options in ARGP.  */
static struct hol *
argp_hol (const struct argp *argp)
{
  const struct argp **children = argp->children;
  struct hol *hol = make_hol (argp->options);
  if (children)
    while (*children)
      hol_append (hol, argp_hol (*children++));
  return hol;
}

/* Print all the non-option args documented in ARGP to STREAM.  Any output is
   preceded by a space.  */
static void
argp_args_usage (const struct argp *argp, FILE *stream)
{
  const struct argp **children = argp->children;
  const char *doc = argp->args_doc;
  if (doc)
    {
      /* Manually do line wrapping so that it (probably) won't get wrapped at
	 any embedded spaces.  */
      if (line_wrap_point (stream) + 1 + strlen (doc)
	  >= line_wrap_rmargin (stream))
	putc ('\n', stream);
      else
	putc (' ', stream);
      fputs (doc, stream);
    }
  if (children)
    while (*children)
      argp_args_usage (*children++, stream);
}

/* Print the documentation for ARGP to STREAM.  Each separate bit of
   documentation is preceded by a blank line.  */
static void
argp_doc (const struct argp *argp, FILE *stream)
{
  const struct argp **children = argp->children;
  const char *doc = argp->doc;
  if (doc)
    {
      putc ('\n', stream);
      fputs (doc, stream);
      if (line_wrap_point (stream) > line_wrap_lmargin (stream))
	putc ('\n', stream);
    }
  if (children)
    while (*children)
      argp_doc (*children++, stream);
}

/* Output a usage message for ARGP to STREAM.  FLAGS are from the set
   ARGP_HELP_*.  NAME is what to use wherever a `program name' is needed. */
void argp_help (const struct argp *argp, FILE *stream,
		unsigned flags, char *name)
{
  int first = 1;
  struct hol *hol = 0;

  if (! stream)
    return;

  stream = line_wrap_stream (stream, 0, RMARGIN, 0);
  assert (stream);

  if (flags & (ARGP_HELP_USAGE | ARGP_HELP_SHORT_USAGE | ARGP_HELP_LONG))
    {
      hol = argp_hol (argp);

      /* If present, these options always come last.  */
      hol_set_group (hol, "help", -1);
      hol_set_group (hol, "version", -1);

      hol_sort (hol);
    }

  if (flags & (ARGP_HELP_USAGE | ARGP_HELP_SHORT_USAGE))
    /* Print a short `Usage:' message.  */
    {
      int old_lm;
      int old_wm = line_wrap_set_wmargin (stream, USAGE_INDENT);

      fprintf (stream, "Usage: %s", name);

      /* We set the lmargin as well as the wmargin, because hol_usage
	 manually wraps options with newline to avoid annoying breaks.  */
      old_lm = line_wrap_set_lmargin (stream, USAGE_INDENT);

      if (flags & ARGP_HELP_SHORT_USAGE)
	/* Just show where the options go.  */
	{
	  if (hol->num_entries > 0)
	    fputs (" [OPTION...]", stream);
	}
      else
	/* Actually print the options.  */
	hol_usage (hol, stream);
      argp_args_usage (argp, stream);

      line_wrap_set_wmargin (stream, old_wm);
      line_wrap_set_lmargin (stream, old_lm);

      putc ('\n', stream);
      first = 0;
    }

  if (flags & ARGP_HELP_SEE)
    {
      fprintf (stream, "Try `%s --help' for more information.\n", name);
      first = 0;
    }

  if (flags & ARGP_HELP_LONG)
    /* Print a long, detailed help message.  */
    {
      /* Print info about all the options.  */
      if (hol->num_entries > 0)
	{
	  if (! first)
	    putc ('\n', stream);
	  hol_help (hol, stream);
	  first = 0;
	}

      /* Finally, print any documentation strings at the end.  */
      argp_doc (argp, stream);
    }

  if (hol)
    hol_free (hol);

  line_unwrap_stream (stream);
}

/* Output, if appropriate, a usage message for STATE to STREAM.  FLAGS are
   from the set ARGP_HELP_*.  */
void
argp_state_help (struct argp_state *state, FILE *stream, unsigned flags)
{
  if ((!state || ! (state->flags & ARGP_NO_ERRS)) && stream)
    {
      argp_help (state ? state->argp : 0, stream, flags,
		 state ? state->name : program_invocation_name);

      if (!state || ! (state->flags & ARGP_NO_EXIT))
	{
	  if (flags & ARGP_HELP_EXIT_ERR)
	    exit (1);
	  if (flags & ARGP_HELP_EXIT_OK)
	    exit (0);
	}
  }
}

/* If appropriate, print the printf string FMT and following args, preceded
   by the program name and `:', to stderr, and followed by a `Try ... --help'
   message, then exit (1).  */
void
argp_error (struct argp_state *state, const char *fmt, ...)
{
  if (!state || !(state->flags & ARGP_NO_ERRS))
    {
      FILE *stream = state ? state->err_stream : stderr;

      if (stream)
	{
	  va_list ap;

	  fputs (program_invocation_name, stream);
	  putc (':', stream);
	  putc (' ', stream);

	  va_start (ap, fmt);
	  vfprintf (stream, fmt, ap);
	  va_end (ap);

	  putc ('\n', stream);

	  argp_state_help (state, stream, ARGP_HELP_STD_ERR);
	}
    }
}

/* Similar to the standard gnu error-reporting function error(), but will
   respect the ARGP_NO_EXIT and ARGP_NO_ERRS flags in STATE, and will print
   to STATE->err_stream.  This is useful for argument parsing code that is
   shared between program startup (when exiting is desired) and runtime
   option parsing (when typically an error code is returned instead).  The
   difference between this function and argp_error is that the latter is for
   *parsing errors*, and the former is for other problems that occur during
   parsing but don't reflect a (syntactic) problem with the input.  */
void
argp_failure (struct argp_state *state, int status, int errnum,
	      const char *fmt, ...)
{
  if (!state || !(state->flags & ARGP_NO_ERRS))
    {
      FILE *stream = state ? state->err_stream : stderr;

      if (stream)
	{
	  fputs (state ? state->name : program_invocation_name, stream);

	  if (fmt)
	    {
	      va_list ap;

	      putc (':', stream);
	      putc (' ', stream);

	      va_start (ap, fmt);
	      vfprintf (stream, fmt, ap);
	      va_end (ap);
	    }

	  if (errnum)
	    {
	      putc (':', stream);
	      putc (' ', stream);
	      fputs (strerror (errnum), stream);
	    }

	  putc ('\n', stream);

	  if (status)
	    exit (status);
	}
    }
}
