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

static struct argp_option options[] =
{
  {"kind",        'k', 0, 0, "print the type of store behind FILE"},
  {"name",        'n', 0, 0, "print the name of the store behind FILE"},
  {"blocks",      'b', 0, 0, "print the number of blocks in FILE"},
  {"block-size",  'B', 0, 0, "print the block size of FILE's store"},
  {"size",        's', 0, 0, "print the size, in bytes, of FILE"},
  {"runs",        'r', 0, 0, "print the runs of blocks in FILE"},
  {"dereference", 'L', 0, 0, "if FILE is a symbolic link, follow it"},
  {"prefix",      'p', 0, 0, "always print `FILE: ' before info"},
  {"no-prefix",   'P', 0, 0, "never print `FILE: ' before info"},
  {0, 0}
};
static char *args_doc = "FILE...";
static char *doc = "With no FILE arguments, the file attached to standard \
input is used.  The fields to be printed are separated by colons, in this \
order: PREFIX: KIND: NAME: BLOCK-SIZE: BLOCKS: SIZE: RUNS.  By default, all \
fields are printed.";

/* ---------------------------------------------------------------- */

/* Things we can print about a file's storage.  */
#define W_SOURCE	0x01
#define W_KIND		0x02
#define W_NAME		0x04
#define W_BLOCKS	0x08
#define W_BLOCK_SIZE	0x10
#define W_SIZE		0x20
#define W_RUNS		0x40

#define W_ALL		0xFF

/* Print a line of storage information for NODE to stdout.  If PREFIX is
   non-NULL, print it first, followed by a colon.  */
static error_t
print_info (mach_port_t node, char *source, unsigned what)
{
  error_t err;
  int flags;
  int first = 1;
  int kind, i;
  char *kind_name;
  char *misc;
  off_t *runs;
  unsigned misc_len, runs_len;
  size_t block_size;
  off_t blocks = 0, size = 0;
  string_t name;
  mach_port_t store_port;
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
      if (str && (what & mask) == mask)
	{
	  psep ();
	  fputs (str, stdout);
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

  err = file_get_storage_info (node, &kind, &runs, &runs_len, &block_size,
			       name, &store_port, &misc, &misc_len, &flags);
  if (err)
    return err;
  mach_port_deallocate (mach_task_self (), store_port);
  vm_deallocate (mach_task_self (), (vm_address_t)misc, misc_len);

  switch (kind)
    {
    case STORAGE_OTHER: kind_name = "other"; break;
    case STORAGE_DEVICE: kind_name = "device"; break;
    case STORAGE_HURD_FILE: kind_name = "file"; break;
    case STORAGE_NETWORK: kind_name = "network"; break;
    case STORAGE_MEMORY: kind_name = "memory"; break;
    case STORAGE_TASK: kind_name = "task"; break;
    default:
      sprintf (unknown_kind_name, "%d", kind);
      kind_name = unknown_kind_name;
    }

  for (i = 0; i < runs_len; i += 2)
    {
      if (runs[i] >= 0)
	blocks += runs[i+1];
      size += runs[i+1];
    }
  size *= block_size;

  pstr (source,	    0);
  pstr (kind_name,  W_KIND);
  if ((flags & STORAGE_MUTATED) && (what & W_KIND))
    fputs ("/mutated", stdout);
  pstr (name,       W_NAME);
  pint (block_size, W_BLOCK_SIZE);
  pint (blocks,     W_BLOCKS);
  pint (size,       W_SIZE);

  if (what & W_RUNS)
    {
      psep ();
      for (i = 0; i < runs_len; i += 2)
	{
	  if (i > 0)
	    putchar (',');
	  if (runs[i] < 0)
	    printf ("[%ld]", runs[i+1]);
	  else
	    printf ("%ld[%ld]", runs[i], runs[i+1]);
	}
    }

  putchar ('\n');

  return 0;
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
	  if (print_prefix < 0)
	    /* By default, only print filename prefixes for multiple files. */
	    print_prefix = state->next < state->argc;
	  if (what == 0)
	    what = W_ALL;
	  if (file == MACH_PORT_NULL)
	    error (3, err, source);
	  err = print_info (file, print_prefix ? source : 0, what);
	  if (err)
	    error (4, err, source);
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

	case ARGP_KEY_NO_ARGS:
	  info (getdport (0), "-", 0); break;
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
