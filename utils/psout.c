/* Common output function for ps & w

   Copyright (C) 1995, 1996, 1998 Free Software Foundation, Inc.

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <error.h>
#include <ps.h>

void
psout (struct proc_stat_list *procs,
       char *fmt_string, int posix_fmt, struct ps_fmt_specs *specs,
       char *sort_key_name, int sort_reverse,
       int output_width, int print_heading,
       int squash_bogus_fields, int squash_nominal_fields,
       int top)
{
  error_t err;
  struct ps_stream *output;
  struct ps_fmt *fmt;

  err = ps_fmt_create (fmt_string, posix_fmt, specs, &fmt);
  if (err)
    {
      char *problem;
      ps_fmt_creation_error (fmt_string, posix_fmt, specs, &problem);
      error (4, 0, "%s", problem);
    }

  if (squash_bogus_fields)
    /* Remove any fields that we can't print anyway (because of system
       bugs/protection violations?).  */
    {
      ps_flags_t bogus_flags = ps_fmt_needs (fmt);

      err = proc_stat_list_find_bogus_flags (procs, &bogus_flags);
      if (err)
	error (0, err, "Couldn't remove bogus fields");
      else
	ps_fmt_squash_flags (fmt, bogus_flags);
    }

  if (squash_nominal_fields)
    /* Remove any fields that contain only `uninteresting' information.  */
    {
      int nominal (struct ps_fmt_field *field)
	{
	  return !(field->flags & PS_FMT_FIELD_KEEP)
	    && proc_stat_list_spec_nominal (procs, field->spec);
	}
      ps_fmt_squash (fmt, nominal);
    }

  if (sort_key_name)
    /* Sort on the given field. */
    {
      const struct ps_fmt_spec *sort_key;

      if (*sort_key_name == '-')
	/* Sort in reverse.  */
	{
	  sort_reverse = 1;
	  sort_key_name++;
	}

      sort_key = ps_fmt_specs_find (specs, sort_key_name);
      if (sort_key == NULL)
	error (3, 0, "%s: bad sort key", sort_key_name);

      err = proc_stat_list_sort (procs, sort_key, sort_reverse);
      if (err)
	/* Give an error message, but don't exit.  */
	error (0, err, "Couldn't sort processes");
    }

  err = ps_stream_create (stdout, &output);
  if (err)
    error (5, err, "Can't make output stream");

  if (print_heading)
    {
      if (procs->num_procs > 0)
	{
	  err = ps_fmt_write_titles (fmt, output);
	  if (err)
	    error (0, err, "Can't print titles");
	  ps_stream_newline (output);
	}
      else
	error (1, 0, "No applicable processes");
    }

  if (output_width)
    /* Try and restrict the number of output columns.  */
    {
      int deduce_term_size (int fd, char *type, int *width, int *height);
      if (output_width < 0)
	/* Have to figure it out!  */
	if (! deduce_term_size (1, getenv ("TERM"), &output_width, 0))
	  output_width = 80;	/* common default */
      ps_fmt_set_output_width (fmt, output_width);
    }

  if (top)
    /* Restrict output to the top TOP entries, if TOP is positive, or the
       bottom -TOP entries, if it is negative.  */
    {
      int filter (struct proc_stat *ps)
	{
	  return --top >= 0;
	}
      if (top < 0)
	{
	  top += procs->num_procs;
	  proc_stat_list_filter1 (procs, filter, 0, 1);
	}
      else
	proc_stat_list_filter1 (procs, filter, 0, 0);
    }

  /* Finally, output all the processes!  */
  err = proc_stat_list_fmt (procs, fmt, output);
  if (err)
    error (5, err, "Couldn't output process status");
}
