/* Show where a file exists

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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
#include <argp.h>
#include <unistd.h>
#include <sys/fcntl.h>

#include <error.h>

#include <hurd/fs.h>
#include <hurd/store.h>

static struct argp_option options[] =
{
  {"kind",        'k', 0, 0, "Print the type of store behind FILE"},
  {"name",        'n', 0, 0, "Print the name of the store behind FILE"},
  {"blocks",      'b', 0, 0, "Print the number of blocks in FILE"},
  {"block-size",  'B', 0, 0, "Print the block size of FILE's store"},
  {"size",        's', 0, 0, "Print the size, in bytes, of FILE"},
  {"runs",        'r', 0, 0, "Print the runs of blocks in FILE"},
  {"children",	  'c', 0, 0, "If the store has children, show them too"},
  {"dereference", 'L', 0, 0, "If FILE is a symbolic link, follow it"},
  {"prefix",      'p', 0, 0, "Always print `FILE: ' before info"},
  {"no-prefix",   'P', 0, 0, "Never print `FILE: ' before info"},
  {0, 0}
};
static char *args_doc = "FILE...";
static char *doc = "With no FILE arguments, the file attached to standard \
input is used.  The fields to be printed are separated by colons, in this \
order: PREFIX: KIND: NAME: BLOCK-SIZE: BLOCKS: SIZE: RUNS.  If the store is a \
composite one and --children is specified, children are printed on lines \
following the main store, indented accordingly.  By default, all \
fields, and children, are printed.";

/* ---------------------------------------------------------------- */

/* Things we can print about a file's storage.  */
#define W_SOURCE	0x01
#define W_KIND		0x02
#define W_NAME		0x04
#define W_BLOCKS	0x08
#define W_BLOCK_SIZE	0x10
#define W_SIZE		0x20
#define W_RUNS		0x40
#define W_CHILDREN	0x80

#define W_ALL		0xFF

/* Print a line of information (exactly what is determinted by WHAT)
   about store to stdout.  LEVEL is the desired indentation level.  */
static void
print_store (struct store *store, int level, unsigned what)
{
  int i;
  int first = 1;
  char *kind_name;
  char unknown_kind_name[20];

  void psep ()
    {
      if (first)
	first = 0;
      else
	{
	  putchar (':');
	  putchar (' ');
	}
    }
  void pstr (char *str, unsigned mask)
    {
      if ((what & mask) == mask)
	{
	  psep ();
	  fputs (str ?: "-", stdout);
	}
    }
  void pint (off_t val, unsigned mask)
    {
      if ((what & mask) == mask)
	{
	  psep ();
	  printf ("%ld", val);
	}
    }

  switch (store->class)
    {
    case STORAGE_OTHER: kind_name = "other"; break;
    case STORAGE_DEVICE: kind_name = "device"; break;
    case STORAGE_HURD_FILE: kind_name = "file"; break;
    case STORAGE_NETWORK: kind_name = "network"; break;
    case STORAGE_MEMORY: kind_name = "memory"; break;
    case STORAGE_TASK: kind_name = "task"; break;
    case STORAGE_NULL: kind_name = "null"; break;
    case STORAGE_CONCAT: kind_name = "concat"; break;
    case STORAGE_LAYER: kind_name = "layer"; break;
    case STORAGE_INTERLEAVE: kind_name = "interleave"; break;
    default:
      sprintf (unknown_kind_name, "%d", store->class);
      kind_name = unknown_kind_name;
    }

  for (i = 0; i < level; i++)
    {
      putchar (' ');
      putchar (' ');
    }
  pstr (kind_name,  W_KIND);
  if ((store->flags & STORAGE_MUTATED) && (what & W_KIND))
    fputs ("/mutated", stdout);
  pstr (store->name,       W_NAME);
  pint (store->block_size, W_BLOCK_SIZE);
  pint (store->blocks,     W_BLOCKS);
  pint (store->size,       W_SIZE);

  if (what & W_RUNS)
    {
      psep ();
      for (i = 0; i < store->num_runs; i++)
	{
	  if (i > 0)
	    putchar (',');
	  if (store->runs[i].start < 0)
	    printf ("[%ld]", store->runs[i].length);
	  else
	    printf ("%ld[%ld]", store->runs[i].start, store->runs[i].length);
	}
    }

  putchar ('\n');

  if (what & W_CHILDREN)
    /* Show info about stores that make up this one.  */
    for (i = 0; i < store->num_children; i++)
      print_store (store->children[i], level + 1, what);
}

void 
main(int argc, char *argv[])
{
  int deref = 0, print_prefix = -1;
  unsigned what = 0;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      void info (mach_port_t file, char *source, error_t err)
	{
	  struct store *store;

	  if (file == MACH_PORT_NULL)
	    error (3, err, source);

	  if (print_prefix < 0)
	    /* By default, only print filename prefixes for multiple files. */
	    print_prefix = state->next < state->argc;

	  if (what == 0)
	    what = W_ALL;

	  err = store_create (file, &store);
	  if (err)
	    error (4, err, source);

	  print_store (store, 0, what);
	  store_free (store);
	}

      switch (key)
	{
	case 'L': deref = 1; break;
	case 'p': print_prefix = 1; break;
	case 'P': print_prefix = 0; break;

	case 'k': what |= W_KIND; break;
	case 'n': what |= W_NAME; break;
	case 'b': what |= W_BLOCKS; break;
	case 'B': what |= W_BLOCK_SIZE; break;
	case 's': what |= W_SIZE; break;
	case 'r': what |= W_RUNS; break;
	case 'c': what |= W_CHILDREN; break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);

	case ARGP_KEY_ARG:
	  if (strcmp (arg, "-") == 0)
	    info (getdport (0), "-", 0);
	  else
	    {
	      file_t file = file_name_lookup (arg, deref ? 0 : O_NOLINK, 0);
	      info (file, arg, errno);
	      mach_port_deallocate (mach_task_self (), file);
	    }
	  break;

	default:
	  return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0, 0);

  exit(0);
}
