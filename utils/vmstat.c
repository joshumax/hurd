/* Print vm statistics

   Copyright (C) 1996 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdio.h>
#include <stddef.h>
#include <argp.h>
#include <error.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <mach.h>
#include <mach/vm_statistics.h>
#include <hurd.h>

char *argp_program_version = "vmstat 1.0 (GNU " HURD_RELEASE ")";

struct field {
  /* Name of the field; used for the option name.  */
  char *name;

  /* A descriptive title used for long output format.  */
  char *desc;

  /* Terse header used for the columnar style output.  */
  char *hdr;

  /* True if this field is `cumulative', that is, monotonic increasing. */
  int cum;

  /* Offset of the integer_t field in a vm_statistics structure */
  int offs;
};

/* Returns the byte offset of the field FIELD in a vm_statistics structure. */
#define FOFFS(field_name)  offsetof (struct vm_statistics, field_name)

/* Yields an lvalue refering to FIELD in STATS.  */
#define FVAL(stats, field) (*(integer_t *)((char *)&(stats) + (field)->offs))

/* vm_statistics fields we know about.  */
static const struct field fields[] = {
  { "pagesize",	   "Pagesize",	     "pgsz",	0, FOFFS (pagesize) },
  { "free",	   "Free pages",     "free",	0, FOFFS (free_count) },
  { "active",	   "Active pages",   "actv",	0, FOFFS (active_count) },
  { "inactive",	   "Inactive pages", "inac",	0, FOFFS (inactive_count) },
  { "wired",	   "Wired pages",    "wire",	0, FOFFS (wire_count) },
  { "zero-filled", "Zeroed pages",   "zeroed",	1, FOFFS (zero_fill_count) },
  { "reactivations","Reactivations", "react",	1, FOFFS (reactivations) },
  { "pageins",	   "Pageins",	     "pgins",	1, FOFFS (pageins) },
  { "pageouts",    "Pageouts",	     "pgouts",	1, FOFFS (pageouts) },
  { "faults",	   "Faults",	     "pfaults", 1, FOFFS (faults) },
  { "cow-faults",  "Cow faults",     "cowpfs",	1, FOFFS (cow_faults) },
  { "cache-lookups","Cache lookups", "clkups",	1, FOFFS (lookups) },
  { "cache-hits",  "Cache hits",     "chits",	1, FOFFS (hits) },
  {0}
};

static const struct argp_option options[] = {
  {"terse",	't',	0, 0, "Use short one-line output format", 1 },
  {"no-header", 'H',    0, 0, "Don't print a descriptive header line"},
  {"prefix",    'p', 0, 0, "Always display a description before stats"},
  {"no-prefix", 'P', 0, 0, "Never display a description before stats"},

  /* A header for all the individual field options.  */
  { 0,0,0,0, "Selecting which statistics to show:", 2},

  {0}
};
static const char *args_doc = "[PERIOD [COUNT [HEADER_INTERVAL]]]";
static const char *doc = "If PERIOD is supplied, then terse mode is"
" selected, and the output repeated every PERIOD seconds, with cumulative"
" fields given the difference from the last output.  If COUNT is given"
" and non-zero, only that many lines are output.  HEADER_INTERVAL"
" defaults to 23, and if not zero, is the number of repeats after which a"
" blank line and the header will be reprinted (as well as the totals for"
" cumulative fields).";

int
main (int argc, char **argv)
{
  error_t err;
  const struct field *field;
  struct vm_statistics stats;
  int num_fields = 0;		/* Number of vm_fields known. */
  unsigned long output_fields = 0; /* A bit per field, from 0. */
  int count = 1;		/* Number of repeats.  */
  unsigned period = 0;		/* Seconds between terse mode repeats.  */
  unsigned hdr_interval = 23;	/*  */

  int terse = 0, print_heading = 1, print_prefix = -1;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      if (key < 0)
	/* A field option.  */
	output_fields |= (1 << (-1 - key));
      else
	switch (key)
	  {
	  case 't': terse = 1; break;
	  case 'p': print_prefix = 1; break;
	  case 'P': print_prefix = 0; break;
	  case 'H': print_heading = 0; break;

	  case ARGP_KEY_ARG:
	    terse = 1;
	    switch (state->arg_num)
	      {
	      case 0:
		period = atoi (arg); count = 0; break;
	      case 1:
		count = atoi (arg); break;
	      case 2:
		hdr_interval = atoi (arg); break;
	      default:
		return ARGP_ERR_UNKNOWN;
	      }
	    break;

	  default:
	    return ARGP_ERR_UNKNOWN;
	  }
      return 0;
    }
  struct argp_option *field_opts;
  int field_opts_size;
  struct argp field_argp = { 0, parse_opt };
  const struct argp *parents[] = { &field_argp, 0 };
  const struct argp argp = { options, parse_opt, args_doc, doc, parents };

  /* See how many fields we know about.  */
  for (field = fields; field->name; field++)
    num_fields++;

  /* Construct an options vector for them.  */
  field_opts_size = ((num_fields + 1) * sizeof (struct argp_option));
  field_opts = alloca (field_opts_size);
  bzero (field_opts, field_opts_size);

  for (field = fields; field->name; field++)
    {
      int which = field - fields;
      struct argp_option *opt = &field_opts[which];

      opt->name = field->name;
      opt->key = -1 - which;	/* options are numbered -1 ... -(N - 1).  */
      opt->doc = field->desc;
      opt->group = 2;
    }
  /* No need to terminate FIELD_OPTS because the bzero above's done so.  */

  field_argp.options = field_opts;

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (output_fields == 0)
    output_fields = ~0;		/* By default, show all fields. */

  if (terse)
    /* Terse (one-line) output mode.  */
    {
      int first_hdr = 1, first, repeats;
      struct vm_statistics prev_stats;

      if (count == 0)
	count = -1;

      do
	{
	  if (first_hdr)
	    first_hdr = 0;
	  else
	    putchar ('\n');

	  if (print_heading)
	    {
	      int which;
	      for (which = 0, first = 1; which < num_fields; which++)
		if (output_fields & (1 << which))
		  {
		    if (first)
		      first = 0;
		    else
		      putchar (' ');
		    fputs (fields[which].hdr, stdout);
		  }
	      putchar ('\n');
	    }
	
	  bzero (&prev_stats, sizeof (prev_stats));

	  for (repeats = 0
	       ; count && repeats < hdr_interval && count
	       ; repeats++, count--)
	    {
	      /* Actually fetch the statistics.  */
	      err = vm_statistics (mach_task_self (), &stats);
	      if (err)
		error (2, err, "vm_statistics");

	      /* Output the fields.  */
	      for (field = fields, first = 1; field->name; field++)
		if (output_fields & (1 << (field - fields)))
		  {
		    integer_t val = FVAL (stats, field);
		    int width = strlen (field->hdr);

		    if (field->cum)
		      {
			integer_t cum = val;
			val -= FVAL (prev_stats, field);
			FVAL (prev_stats, field) = cum;
		      }

		    if (first)
		      first = 0;
		    else
		      putchar (' ');
		    printf ("%*d", width, val);
		  }
	      putchar ('\n');

	      if (period)
		sleep (period);
	    }
	}
      while (count);
    }
  else
    /* Verbose output.  */
    {
      int max_desc_width = 0;

      if (print_prefix < 0)
	/* By default, only print a prefix if there are multiple fields. */
	print_prefix = (output_fields & (output_fields - 1));

      if (print_prefix)
	/* Find the widest description string, so we can align the output. */
	for (field = fields; field->name; field++)
	  if (output_fields & (1 << (field - fields)))
	    {
	      int desc_len = strlen (field->desc);
	      if (desc_len > max_desc_width)
		max_desc_width = desc_len;
	    }

      /* Actually fetch the statistics.  */
      err = vm_statistics (mach_task_self (), &stats);
      if (err)
	error (2, err, "vm_statistics");

      for (field = fields; field->name; field++)
	if (output_fields & (1 << (field - fields)))
	  if (print_prefix)
	    printf ("%s:%*d\n", field->desc,
		    max_desc_width + 5 - strlen (field->desc),
		    FVAL (stats, field));
	  else
	    printf ("%d\n", FVAL (stats, field));
    }

  exit (0);
}
