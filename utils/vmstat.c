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

#include <mach.h>
#include <mach/vm_statistics.h>

struct field {
  /* Name of the field; used for the option name.  */
  char *name;

  /* A descriptive title used for long output format.  */
  char *desc;

  /* Terse header used for the columnar style output.  */
  char *hdr;

  /* Offset of the integer_t field in a vm_statistics structure */
  int offs;
};

/* Returns the byte offset of the field FIELD in a vm_statistics structure. */
#define FOFFS(field)  offsetof (struct vm_statistics, field)

/* vm_statistics fields we know about.  */
static const struct field fields[] = {
  { "pagesize",	   "Pagesize",	     "pgsz",	FOFFS (pagesize) },
  { "free",	   "Free pages",     "free",	FOFFS (free_count) },
  { "active",	   "Active pages",   "actv",	FOFFS (active_count) },
  { "inactive",	   "Inactive pages", "inac",	FOFFS (inactive_count) },
  { "wired",	   "Wired pages",    "wire",	FOFFS (wire_count) },
  { "zero-filled", "Zeroed pages",   "zeroed",	FOFFS (zero_fill_count) },
  { "reactivations","Reactivations", "react",	FOFFS (reactivations) },
  { "pageins",	   "Pageins",	     "pgins",	FOFFS (pageins) },
  { "pageouts",    "Pageouts",	     "pgouts",	FOFFS (pageouts) },
  { "faults",	   "Faults",	     "pfaults",  FOFFS (faults) },
  { "cow-faults",  "Cow faults",     "cowpfs",	FOFFS (cow_faults) },
  { "cache-lookups","Cache lookups", "clkups",	FOFFS (lookups) },
  { "cache-hits",  "Cache hits",     "chits",	FOFFS (hits) },
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
static const char *args_doc = 0;
static const char *doc = 0;

int
main (int argc, char **argv)
{
  error_t err;
  const struct field *field;
  struct vm_statistics stats;
  int num_fields = 0;		/* Number of vm_fields known. */
  unsigned long output_fields = 0; /* A bit per field, from 0. */

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
	  default: return EINVAL;
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
  argp_parse (&argp, argc, argv, 0, 0);

  if (output_fields == 0)
    output_fields = ~0;		/* By default, show all fields. */

  /* Actually fetch the statistics.  */
  err = vm_statistics (mach_task_self (), &stats);
  if (err)
    error (2, err, "vm_statistics");

  /* Print them.  */
  if (terse)
    {
      int first = 1;
      if (print_heading)
	{
	  int which;
	  for (which = 0; which < num_fields; which++)
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
      first = 1;
      for (field = fields; field->name; field++)
	if (output_fields & (1 << (field - fields)))
	  {
	    int width = strlen (field->hdr);
	    if (first)
	      first = 0;
	    else
	      putchar (' ');
	    printf ("%*d", width,
		    *(integer_t *)((char *)&stats + field->offs));
	  }
      putchar ('\n');
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

      for (field = fields; field->name; field++)
	if (output_fields & (1 << (field - fields)))
	  if (print_prefix)
	    printf ("%s:%*d\n",
		    field->desc,
		    max_desc_width + 5 - strlen (field->desc),
		    *(integer_t *)((char *)&stats + field->offs));
	  else
	    printf ("%d\n", *(integer_t *)((char *)&stats + field->offs));
    }

  exit (0);
}
