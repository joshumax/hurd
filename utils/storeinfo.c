/* Show where a file exists

   Copyright (C) 1995,96,97,98,99,2001,02 Free Software Foundation, Inc.

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <version.h>

#include <error.h>

#include <hurd/fs.h>
#include <hurd/store.h>

const char *argp_program_version = STANDARD_HURD_VERSION (storeinfo);

static struct argp_option options[] =
{
  {"type",        't', 0, 0, "Print the type of store behind FILE"},
  {"flags",       'f', 0, 0, "Print the flags associated with FILE's store"},
  {"name",        'n', 0, 0, "Print the name of the store behind FILE"},
  {"blocks",      'b', 0, 0, "Print the number of blocks in FILE"},
  {"block-size",  'B', 0, 0, "Print the block size of FILE's store"},
  {"size",        's', 0, 0, "Print the size, in bytes, of FILE"},
  {"block-list",  'l', 0, 0, "Print the blocks that are in FILE"},
  {"children",	  'c', 0, 0, "If the store has children, show them too"},
  {"dereference", 'L', 0, 0, "If FILE is a symbolic link, follow it"},
  {"prefix",      'p', 0, 0, "Always print `FILE: ' before info"},
  {"no-prefix",   'P', 0, 0, "Never print `FILE: ' before info"},
  {0, 0}
};
static char *args_doc = "FILE...";
static char *doc = "Show information about storage used by FILE..."
"\vWith no FILE arguments, the file attached to standard"
" input is used.  The fields to be printed are separated by colons, in this"
" order: PREFIX: TYPE (FLAGS): NAME: BLOCK-SIZE: BLOCKS: SIZE: BLOCK-LIST."
"  If the store is a composite one and --children is specified, children"
" are printed on lines following the main store, indented accordingly."
"  By default, all fields, and children, are printed.";

/* ---------------------------------------------------------------- */

/* Things we can print about a file's storage.  */
#define W_SOURCE	0x01
#define W_TYPE		0x02
#define W_NAME		0x04
#define W_BLOCKS	0x08
#define W_BLOCK_SIZE	0x10
#define W_SIZE		0x20
#define W_RUNS		0x40
#define W_CHILDREN	0x80
#define W_FLAGS		0x100

#define W_ALL		0x1FF

/* Print a line of information (exactly what is determinted by WHAT)
   about store to stdout.  LEVEL is the desired indentation level.  */
static void
print_store (struct store *store, int level, unsigned what)
{
  int i;
  int first = 1;

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
  void pstr (const char *str, unsigned mask)
    {
      if ((what & mask) == mask)
	{
	  psep ();
	  fputs (str ?: "-", stdout);
	}
    }
  void psiz (size_t val, unsigned mask)
    {
      if ((what & mask) == mask)
	{
	  psep ();
	  printf ("%zu", val);
	}
    }
  void poff (store_offset_t val, unsigned mask)
    {
      if ((what & mask) == mask)
	{
	  psep ();
	  printf ("%Ld", val);
	}
    }

  /* Indent */
  for (i = 0; i < level; i++)
    {
      putchar (' ');
      putchar (' ');
    }

  pstr (store->class->name,W_TYPE);

  if ((store->flags & ~STORE_INACTIVE) && (what & W_FLAGS))
    {
      int t = 0;		/* flags tested */
      int f = 1;
      void pf (int mask, char *name)
	{
	  if (store->flags & mask)
	    {
	      if (f)
		f = 0;
	      else
		putchar (',');
	      fputs (name, stdout);
	    }
	  t |= mask;
	}

      if (! first)
	putchar (' ');
      first = 0;
      putchar ('(');

      pf (STORE_READONLY, "ro");
      pf (STORE_HARD_READONLY, "h_ro");
      pf (STORE_ENFORCED, "enf");
      pf (STORAGE_MUTATED, "mut");

      if (store->flags & ~(t | STORE_INACTIVE))
	/* Leftover flags. */
	{
	  if (! f)
	    putchar (';');
	  printf ("0x%x", store->flags & ~(t | STORE_INACTIVE));
	}
      putchar (')');
    }

  pstr (store->name,       W_NAME);
  psiz (store->block_size, W_BLOCK_SIZE);
  poff (store->blocks,     W_BLOCKS);
  poff (store->size,       W_SIZE);

  if (what & W_RUNS)
    {
      psep ();
      for (i = 0; i < store->num_runs; i++)
	{
	  if (i > 0)
	    putchar (',');
	  if (store->runs[i].start < 0)
	    /* A hole */
	    printf ("@+%Ld", store->runs[i].length);
	  else
	    printf ("%Ld+%Ld", store->runs[i].start, store->runs[i].length);
	}
    }

  putchar ('\n');

  if (what & W_CHILDREN)
    /* Show info about stores that make up this one.  */
    for (i = 0; i < store->num_children; i++)
      print_store (store->children[i], level + 1, what);
}

int
main (int argc, char *argv[])
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
	    error (3, err, "%s", source);

	  if (print_prefix < 0)
	    /* By default, only print filename prefixes for multiple files. */
	    print_prefix = state->next < state->argc;

	  if (what == 0)
	    what = W_ALL;

	  /* The STORE_NO_FILEIO flag tells it to give us the special
	     "unknown" class instead of an error if it cannot parse the
	     file_get_storage_info results.  That will allow us to display
	     what we can from them, i.e. the name that shows at least some
	     of what the unknown data looked like.  */
	  err = store_create (file, STORE_INACTIVE|STORE_NO_FILEIO, 0, &store);
	  if (err)
	    error (4, err, "%s", source);

	  print_store (store, 0, what);
	  store_free (store);
	}

      switch (key)
	{
	case 'L': deref = 1; break;
	case 'p': print_prefix = 1; break;
	case 'P': print_prefix = 0; break;

	case 't': what |= W_TYPE; break;
	case 'f': what |= W_FLAGS; break;
	case 'n': what |= W_NAME; break;
	case 'b': what |= W_BLOCKS; break;
	case 'B': what |= W_BLOCK_SIZE; break;
	case 's': what |= W_SIZE; break;
	case 'l': what |= W_RUNS; break;
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
	    }
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0, 0);

  return 0;
}
