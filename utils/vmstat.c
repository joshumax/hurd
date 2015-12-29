/* Print vm statistics

   Copyright (C) 1996,97,99,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
#include <fcntl.h>
#include <version.h>

#include <mach.h>
#include <mach/gnumach.h>
#include <mach/vm_statistics.h>
#include <mach/vm_cache_statistics.h>
#include <mach/default_pager.h>
#include <hurd.h>
#include <hurd/paths.h>

const char *argp_program_version = STANDARD_HURD_VERSION (vmstat);

static const struct argp_option options[] = {
  {"terse",	't', 0, 0, "Use short one-line output format"},
  {"no-header", 'H', 0, 0, "Don't print a descriptive header line"},
  {"prefix",    'p', 0, 0, "Always display a description before stats"},
  {"no-prefix", 'P', 0, 0, "Never display a description before stats"},
  {"pages",     'v', 0, 0, "Display sizes in pages"},
  {"kilobytes", 'k', 0, 0, "Display sizes in 1024 byte blocks"},
  {"bytes",     'b', 0, 0, "Display sizes in bytes"},
  {0}
};
static const char *args_doc = "[PERIOD [COUNT [HEADER_INTERVAL]]]";
static const char *doc = "Show system virtual memory statistics"
"\vIf PERIOD is supplied, then terse mode is"
" selected, and the output repeated every PERIOD seconds, with cumulative"
" fields given the difference from the last output.  If COUNT is given"
" and non-zero, only that many lines are output.  HEADER_INTERVAL"
" defaults to 23, and if not zero, is the number of repeats after which a"
" blank line and the header will be reprinted (as well as the totals for"
" cumulative fields).";

/* We use this one type to represent all values printed by this program.  It
   should be signed, and hopefully large enough (it may need to be larger
   than what the system returns values in, as we represent some quantities as
   bytes instead of pages)!  */
typedef long long val_t;
#define BADVAL ((val_t) -1LL)	/* a good generic value for "couldn't get" */

/* What a given number describes.  */
enum val_type
{
  COUNT,			/* As-is.  */
  SIZE,				/* Use the most convenient unit, with suffix */
  PAGESZ,			/* Like SIZE, but never changed to PAGES.  */
  PCENT,			/* Append `%'.  */
};

/* Return the `nominal' width of a field of type TYPE, in units of SIZE_UNITS.  */
static size_t
val_width (val_t val, enum val_type type, size_t size_units)
{
  size_t vwidth (val_t val)
    {
      size_t w = 1;
      if (val < 0)
	w++, val = -val;
      while (val > 9)
	w++, val /= 10;
      return w;
    }
  if (type == PCENT)
    return vwidth (val) + 1;
  else if ((type == SIZE || type == PAGESZ) && size_units == 0)
    return val > 1000 ? 5 : vwidth (val) + 1;
  else
    {
      if ((type == SIZE || type == PAGESZ) && size_units > 0)
	val /= size_units;
      return vwidth (val);
    }
}

/* Print a number of type TYPE.  If SIZE_UNITS is non-zero, then values of
   type SIZE are divided by that amount and printed without a suffix.  FWIDTH
   is the width of the field to print it in, right-justified.  If SIGN is
   true, the value is always printed with a sign, even if it's positive.  */
static void
print_val (val_t val, enum val_type type,
	   size_t size_units, int fwidth, int sign)
{
  if (type == PCENT)
    printf (sign ? "%+*lld%%" : "%*lld%%", fwidth - 1, val);
  else if ((type == SIZE || type == PAGESZ) && size_units == 0)
    {
      float fval = val;
      char *units = " KMGT", *u = units;

      while (fval >= 10000)
	{
	  fval /= 1024;
	  u++;
	}

      printf ((fval >= 1000
	       ? (sign ? "%+*.0f%c" : "%*.0f%c")
	       : (sign ? "%+*.3g%c"  : "%*.3g%c")),
	      fwidth - 1, fval, *u);
    }
  else
    {
      if ((type == SIZE || type == PAGESZ) && size_units > 0)
	val /= size_units;
      printf (sign ? "%+*lld" : "%*lld", fwidth, val);
    }
}

/* Special values for val_t ranges.  */
#define VAL_MAX_MEM   -1	/* up to the system memory size */
#define VAL_MAX_SWAP  -2	/* up to the system swap size */

/* How this field changes with time.  */
enum field_change_type
{
  VARY,				/* Can go up or down.  */
  CONST,			/* Always the same.  */
  CUMUL,			/* Monotonic increasing.  */
};

struct vm_state;		/* fwd */

struct field
{
  /* Name of the field.  */
  char *name;

  /* Terse header used for the columnar style output.  */
  char *hdr;

  /* A description of this field (for user help).  */
  char *doc;

  /* Type of this field.  */
  enum field_change_type change_type;

  /* How to display the number associated with this field.
     If this is anything but `DIMLESS', then it can be overriden by the
     user.  */
  enum val_type type;

  /* The `maximum value' this field can have -- used for field widths.  */
  val_t max;

  /* True if we display this field by default (user can always override). */
  int standard :1;

  /* Offset of the integer_t field in a vm_statistics structure.  -1 if a
     computed field (in which case the COMPUTE field should be filled in).  */
  int offs;

  /* How to compute this field.  If 0, get_vmstats_value is used.  This
     function should return a negative number if there's some problem getting
     the field.  */
  val_t (*compute)(struct vm_state *state, const struct field *field);
};

/* State about system vm from which we compute the above defined fields.  */
struct vm_state
{
  /* General vm statistics.  */
  struct vm_statistics vmstats;

  /* Page cache statistics.  */
  struct vm_cache_statistics cache_stats;

  /* default pager port (must be privileged to fetch this).  */
  mach_port_t def_pager;
  struct default_pager_info def_pager_info;
};

static error_t
vm_state_refresh (struct vm_state *state)
{
  error_t err = vm_statistics (mach_task_self (), &state->vmstats);

  if (err)
    return err;

  err = vm_cache_statistics (mach_task_self (), &state->cache_stats);

  if (err)
    return err;

  /* Mark the info as invalid, but leave DEF_PAGER alone.  */
  memset (&state->def_pager_info, 0, sizeof state->def_pager_info);

  return 0;
}

static val_t
get_vmstats_field (struct vm_state *state, const struct field *field)
{
  val_t val =
    (val_t)(*(integer_t *)((char *)&state->vmstats + field->offs));
  if (field->type == SIZE)
    val *= state->vmstats.pagesize;
  return val;
}

static val_t
get_size (struct vm_state *state, const struct field *field)
{
  return
    (state->vmstats.free_count + state->vmstats.active_count
     + state->vmstats.inactive_count + state->vmstats.wire_count)
    * state->vmstats.pagesize;
}

static val_t
vm_state_get_field (struct vm_state *state, const struct field *field)
{
  return (field->compute ?: get_vmstats_field) (state, field);
}

static val_t
get_memobj_hit_ratio (struct vm_state *state, const struct field *field)
{
  return (val_t)
    ((float) state->vmstats.hits * 100. / (float) state->vmstats.lookups);
}

/* Makes sure STATE contains a default pager port and associated info, and
   returns 0 if not (after printing an error).  */
static int
ensure_def_pager_info (struct vm_state *state)
{
  error_t err;

  if (state->def_pager == MACH_PORT_NULL)
    {
      mach_port_t host;

      err = get_privileged_ports (&host, 0);
      if (err == EPERM)
	{
	  /* We are not root, so try opening the /servers file.  */
	  state->def_pager = file_name_lookup (_SERVERS_DEFPAGER, O_READ, 0);
	  if (state->def_pager == MACH_PORT_NULL)
	    {
	      error (0, errno, _SERVERS_DEFPAGER);
	      return 0;
	    }
	}
      if (state->def_pager == MACH_PORT_NULL)
	{
	  if (err)
	    {
	      error (0, err, "get_privileged_ports");
	      return 0;
	    }

	  err = vm_set_default_memory_manager (host, &state->def_pager);
	  mach_port_deallocate (mach_task_self (), host);

	  if (err)
	    {
	      error (0, err, "vm_set_default_memory_manager");
	      return 0;
	    }
	}
    }

  if (!MACH_PORT_VALID (state->def_pager))
    {
      if (state->def_pager == MACH_PORT_NULL)
	{
	  error (0, 0,
		 "No default pager running, so no swap information available");
	  state->def_pager = MACH_PORT_DEAD; /* so we don't try again */
	}
      return 0;
    }

  err = default_pager_info (state->def_pager, &state->def_pager_info);
  if (err)
    error (0, err, "default_pager_info");
  return (err == 0);
}

#define SWAP_FIELD(getter, expr) \
  static val_t getter (struct vm_state *state, const struct field *field) \
  { return ensure_def_pager_info (state) ? (val_t) (expr) : BADVAL; }

SWAP_FIELD (get_swap_size, state->def_pager_info.dpi_total_space)
SWAP_FIELD (get_swap_free, state->def_pager_info.dpi_free_space)
SWAP_FIELD (get_swap_page_size, state->def_pager_info.dpi_page_size)
SWAP_FIELD (get_swap_active, (state->def_pager_info.dpi_total_space
			      - state->def_pager_info.dpi_free_space))

/* Returns the byte offset of the field FIELD in a vm_state structure. */
#define _F(field_name)  offsetof (struct vm_state, field_name)

#define K 1024
#define M (1024*K)
#define G (1024LL*M)

/* vm_statistics fields we know about.  */
static const struct field fields[] =
{
  {"pagesize",	   "pgsz", "System pagesize",
   CONST, PAGESZ, 16*K,		1, _F (vmstats.pagesize) },
  {"size",	   "size", "Usable physical memory",
   CONST, SIZE,   VAL_MAX_MEM,	1, 0, get_size },
  {"free",	   "free", "Unused physical memory",
   VARY,  SIZE,   VAL_MAX_MEM,	1, _F (vmstats.free_count) },
  {"active",	   "actv", "Physical memory in active use",
   VARY,  SIZE,   VAL_MAX_MEM,	1, _F (vmstats.active_count) },
  {"inactive", 	   "inact", "Physical memory in the inactive queue",
   VARY,  SIZE,   VAL_MAX_MEM,	1, _F (vmstats.inactive_count) },
  {"wired",    	   "wired", "Unpageable physical memory",
   VARY,  SIZE,   VAL_MAX_MEM,	1, _F (vmstats.wire_count) },
  {"zero filled",  "zeroed","Cumulative zero-filled pages",
   CUMUL, SIZE,   90*G,		1, _F (vmstats.zero_fill_count) },
  {"reactivated",  "react", "Cumulative reactivated inactive pages",
   CUMUL, SIZE,   900*M,	1, _F (vmstats.reactivations) },
  {"pageins",	   "pgins", "Cumulative pages paged in",
   CUMUL, SIZE,   90*G,		1, _F (vmstats.pageins) },
  {"pageouts",	   "pgouts","Cumulative pages paged out",
   CUMUL, SIZE,   90*G,		1, _F (vmstats.pageouts) },
  {"page faults",  "pfaults","Cumulative page faults",
   CUMUL, COUNT,  99999999,	1, _F (vmstats.faults) },
  {"cow faults",   "cowpfs", "Cumulative copy-on-write page faults",
   CUMUL, COUNT,  9999999,	1, _F (vmstats.cow_faults) },
  {"memobj lookups","lkups","Memory-object lookups",
   CUMUL, COUNT,  999999,	0, _F (vmstats.lookups) },
  {"memobj hits",   "hits", "Memory-object lookups with active pagers",
   CUMUL, COUNT,  999999,	0, _F (vmstats.hits) },
  {"memobj hit ratio","hrat","Percentage of memory-object lookups with active pagers",
   VARY, PCENT,   99,		1, -1, get_memobj_hit_ratio },
  {"cached memobjs", "caobj", "Number of memory-objects retained in the page cache",
   VARY,  COUNT,  99999999,     1, _F (cache_stats.cache_object_count) },
  {"cache",        "cache", "Physical memory used by the page cache",
   VARY,  SIZE,   VAL_MAX_MEM,  1, _F (cache_stats.cache_count) },
  {"swap size",	   "swsize", "Size of the default-pager swap area",
   CONST, SIZE,   VAL_MAX_SWAP,	1, 0 ,get_swap_size },
  {"swap active",  "swactv", "Default-pager swap area in use",
   VARY,  SIZE,   VAL_MAX_SWAP,	0, 0 ,get_swap_active },
  {"swap free",	   "swfree", "Default-pager swap area available for swapping",
   VARY,  SIZE,   VAL_MAX_SWAP,	1, 0 ,get_swap_free },
  {"swap pagesize","swpgsz", "Units used for swapping to the default pager",
   CONST, PAGESZ, 16*K,		0, 0 ,get_swap_page_size },
  {0}
};
#undef _F

/* Convert a field name to the corresponding user-option.  */
static char *name_to_option (const char *name)
{
  char *opt = strdup (name), *p;
  if (opt)
    for (p = opt; *p; p++)
      if (*p == ' ')
	*p = '-';
  return opt;
}

int
main (int argc, char **argv)
{
  error_t err;
  const struct field *field;
  struct vm_state state;
  int num_fields = 0;		/* Number of vm_fields known. */
  unsigned long output_fields = 0; /* A bit per field, from 0. */
  int count = 1;		/* Number of repeats.  */
  unsigned period = 0;		/* Seconds between terse mode repeats.  */
  unsigned hdr_interval = 22;	/*  */
  ssize_t size_units = 0;	/* -1 means `pages' */
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
	  case 'b': size_units = 1; break;
	  case 'v': size_units = -1; break;
	  case 'k': size_units = K; break;

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
  const struct argp_child children[] =
    {{&field_argp, 0, "Selecting which statistics to show:"}, {0}};
  const struct argp argp = { options, parse_opt, args_doc, doc, children };

  /* See how many fields we know about.  */
  for (field = fields; field->name; field++)
    num_fields++;

  /* Construct an options vector for them.  */
  field_opts_size = ((num_fields + 1) * sizeof (struct argp_option));
  field_opts = alloca (field_opts_size);
  memset (field_opts, 0, field_opts_size);

  for (field = fields; field->name; field++)
    {
      int which = field - fields;
      struct argp_option *opt = &field_opts[which];

      opt->name = name_to_option (field->name);
      opt->key = -1 - which;	/* options are numbered -1 ... -(N - 1).  */
      opt->doc = field->doc;
      opt->group = 2;
    }
  /* No need to terminate FIELD_OPTS because the memset above has done so.  */

  field_argp.options = field_opts;

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (output_fields == 0)
    /* Show default fields.  */
    for (field = fields; field->name; field++)
      if (field->standard)
	output_fields |= (1 << (field - fields));

  /* Returns an appropriate SIZE_UNITS for printing FIELD.  */
#define SIZE_UNITS(field)						      \
  (size_units >= 0							      \
   ? size_units								      \
   : ((field)->type == PAGESZ ? 0 : state.vmstats.pagesize))

#define PVAL(val, field, width, sign) \
    print_val (val, (field)->type, SIZE_UNITS (field), width, sign)
    /* Intuit the likely maximum field width of FIELD.  */
#define FWIDTH(field)							      \
    val_width ((field)->max == VAL_MAX_MEM ? get_size (&state, field)	      \
	       : (field)->max == VAL_MAX_SWAP ? get_swap_size (&state, field) \
	       : (field)->max,						      \
	       (field)->type, SIZE_UNITS (field))

  /* Actually fetch the statistics.  */
  memset (&state, 0, sizeof (state)); /* Initialize STATE.  */
  err = vm_state_refresh (&state);
  if (err)
    error (2, err, "vm_state_refresh");

  if (terse)
    /* Terse (one-line) output mode.  */
    {
      int first_hdr = 1, first, repeats;
      struct vm_state prev_state;
      int const_fields = 0;

      if (count == 0)
	count = -1;

      /* We only show const fields once per page, so find out which ones
	 those are.  */
      for (field = fields; field->name; field++)
	if ((output_fields & (1 << (field - fields)))
	    && field->change_type == CONST)
	  const_fields |= (1 << (field - fields));
      output_fields &= ~const_fields;

      if (const_fields)
	hdr_interval--;		/* Allow room for the constant fields.  */

      do
	{
	  int num;
	  int fwidths[num_fields];

	  if (first_hdr)
	    first_hdr = 0;
	  else
	    putchar ('\n');

	  if (const_fields)
	    /* Output constant fields on a line preceding the header.  */
	    {
	      for (field = fields, first = 1; field->name; field++)
		if (const_fields & (1 << (field - fields)))
		  {
		    val_t val = vm_state_get_field (&state, field);
		    if (val < 0)
		      /* Couldn't fetch this field, don't try again.  */
		      const_fields &= ~(1 << (field - fields));
		    else
		      {
                        if (first)
                          {
                            first = 0;
                            fputs("(", stdout);
                          }
                        else
                          fputs(",", stdout);
			printf ("%s: ", field->name);
			PVAL (val, field, 0, 0);
		      }
		  }
	      if (! first)
		puts (")");
	    }

	  /* Calculate field widths.  */
	  for (field = fields, num = 0; field->name; field++, num++)
	    if (output_fields & (1 << (field - fields)))
	      {
		fwidths[num] = FWIDTH (field);
		if (count != 1 && size_units == 0
		    && field->change_type == CUMUL && field->type == SIZE)
		  /* We may be printing a `+' prefix for field changes, and
		     since this is using the mostly constant-width SIZE
		     notation, individual changes may be the same width as
		     appropriated for absolute values -- so reserver another
		     column for the `+' character.  */
		  fwidths[num]++;
		if (fwidths[num] < strlen (field->hdr))
		  fwidths[num] = strlen (field->hdr);
	      }

	  if (print_heading)
	    {
	      for (field = fields, num = 0, first = 1; field->name; field++, num++)
		if (output_fields & (1 << (field - fields)))
		  {
                    if (first)
                      first = 0;
                    else
                      fputs (" ", stdout);
		    fprintf (stdout, "%*s", fwidths[num], field->hdr);
		  }
	      putchar ('\n');
	    }

	  prev_state = state;

	  for (repeats = 0
	       ; count && repeats < hdr_interval
	       ; repeats++, count--)
	    {
	      /* Output the fields.  */
	      for (field = fields, num = 0, first = 1; field->name; field++, num++)
		if (output_fields & (1 << (field - fields)))
		  {
		    val_t val = vm_state_get_field (&state, field);

		    if (val < 0)
		      /* Couldn't fetch this field, don't try again.  */
		      const_fields &= ~(1 << (field - fields));
		    else
		      {
			int sign = 0;

			if (repeats && field->change_type == CUMUL)
			  {
			    sign = 1;
			    val -= vm_state_get_field (&prev_state, field);
			  }

                        if (first)
                          first = 0;
                        else
                          fputs (" ", stdout);
			PVAL (val, field, fwidths[num], sign);
		      }
		  }
	      putchar ('\n');

	      prev_state = state;

	      if (period)
		{
		  sleep (period);
		  err = vm_state_refresh (&state);
		  if (err)
		    error (2, err, "vm_state_refresh");
		}
	    }
	}
      while (count);
    }
  else
    /* Verbose output.  */
    {
      int max_width = 0;

      if (print_prefix < 0)
	/* By default, only print a prefix if there are multiple fields. */
	print_prefix = (output_fields & (output_fields - 1));

      if (print_prefix)
	/* Find the widest description string, so we can align the output. */
	for (field = fields; field->name; field++)
	  if (output_fields & (1 << (field - fields)))
	    {
	      int width = strlen (field->name) + FWIDTH (field);
	      if (width > max_width)
		max_width = width;
	    }

      for (field = fields; field->name; field++)
	if (output_fields & (1 << (field - fields)))
	  {
	    val_t val = vm_state_get_field (&state, field);
	    if (val >= 0)
	      {
		int fwidth = 0;
		if (print_prefix)
		  {
		    printf ("%s: ", field->name);
		    fwidth = max_width - strlen (field->name);
		  }
		PVAL (val, field, fwidth, 0);
		putchar ('\n');
	      }
	  }
    }

  exit (0);
}
