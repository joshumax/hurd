/* vmallocate -- a utility to allocate memory.

   Copyright (C) 2015,2016 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <argp.h>
#include <assert-backtrace.h>
#include <error.h>
#include <hurd.h>
#include <inttypes.h>
#include <mach.h>
#include <mach/vm_param.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <version.h>



const char *argp_program_version = STANDARD_HURD_VERSION (vmallocate);

int verbose;
int do_pause;
int do_read;
int do_write;

uint64_t size;
uint64_t chunk_size = 1U << 30;	/* 1 gigabyte */
uint64_t allocated;

static const struct argp_option options[] =
{
  {NULL, 0, NULL, OPTION_DOC, "Mapping options", 1},
  {"read", 'r', NULL, 0, "read from mapping", 1},
  {"write", 'w', NULL, 0, "write to mapping", 1},

  {NULL, 0, NULL, OPTION_DOC, "Options", 2},
  {"verbose", 'v', NULL, 0, "be more verbose", 2},
  {"pause", 'p', NULL, 0, "read newline from stdin before exiting", 2},
  {0}
};

#define UNITS	"gGmMkKpP"
static const char args_doc[] = "SIZE["UNITS"]";
static const char doc[] = "Allocates memory.\v"
  "This is a stress-test for the vm system.";

/* Parse our options...	 */
error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  char *end;
  switch (key)
    {
    case 'r':
      do_read = 1;
      break;

    case 'w':
      do_write = 1;
      break;

    case 'v':
      verbose += 1;
      break;

    case 'p':
      do_pause = 1;
      break;

    case ARGP_KEY_ARG:
      if (size > 0)
        argp_error (state, "Extra argument after size: %s", arg);

      size = strtoull (arg, &end, 10);
      if (arg == end || (end[0] != 0 && end[1] != 0))
        argp_error (state, "Could not parse size `%s'.", arg);

      switch (end[0]) {
      case 0:
	break;

      case 'g':
      case 'G':
	size <<= 30;
	break;

      case 'm':
      case 'M':
	size <<= 20;
	break;

      case 'k':
      case 'K':
	size <<= 10;
	break;

      case 'p':
      case 'P':
	size <<= PAGE_SHIFT;
	break;

      default:
        argp_error (state,
		    "Unknown unit `%c', expected one of "UNITS".",
		    end[0]);
      }
      break;

    case ARGP_KEY_NO_ARGS:
      argp_usage (state);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

const struct argp argp =
  {
  options: options,
  parser: parse_opt,
  args_doc: args_doc,
  doc: doc,
  };



#define min(a, b)	((a) < (b) ? (a) : (b))

struct child
{
  task_t task;
  struct child *next;
};

int
main (int argc, char **argv)
{
  error_t err;
  int nchildren = 0;
  struct child *c, *children = NULL;
  process_t proc = getproc ();

  /* We must make sure that chunk_size fits into vm_size_t.  */
  assert_backtrace (chunk_size <= 1U << (sizeof (vm_size_t) * 8 - 1));

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (verbose)
    fprintf (stderr, "About to allocate %"PRIu64" bytes in chunks of "
             "%"PRIu64" bytes.\n", size, chunk_size);

  while (allocated < size)
    {
      task_t task;
      uint64_t s = min (chunk_size, size - allocated);
      vm_address_t address = 0;
      volatile char *p;
      char **argv = NULL;

      err = vm_allocate (mach_task_self (), &address, (vm_size_t) s,
                         1 /* anywhere */);
      if (err)
	error (1, err, "vm_allocate");

      /* Write an argument and environment vector.  */
      if (s > 128)
        {
          int written;
          char *arg0;

          arg0 = (char *) address;
          written = sprintf (arg0, "[vmallocate %d]", nchildren);

          argv = (char **) (address + (unsigned) written + 1);
          argv[0] = arg0;
          argv[1] = NULL;
        }

      for (p = (char *) address + PAGE_SIZE;
           p < (char *) (address + (size_t) s);
           p += PAGE_SIZE)
        {
          if (do_read)
            (void) *p;
          if (do_write)
            *p = 1;

          if (verbose > 1
              && ((unsigned int) (p - address) & ((1U<<20) - 1)) == 0)
            fprintf (stderr, "\r%"PRIu64,
                     allocated
                     + ((uint64_t) (uintptr_t) p - (uint64_t) address));
        }

      err = vm_inherit (mach_task_self (), address, (vm_size_t) s,
			VM_INHERIT_COPY);
      if (err)
	error (1, err, "vm_inherit");

      err = task_create (mach_task_self (), 1, &task);
      if (err)
	error (1, err, "task_create");

      err = proc_child (proc, task);
      if (err)
	error (1, err, "proc_child");

      if (argv != NULL)
        {
          process_t childp;

          err = proc_task2proc (proc, task, &childp);
          if (err)
            error (1, err, "proc_task2proc");

          err = proc_set_arg_locations (childp,
                                        (vm_offset_t) &argv[0],
                                        (vm_offset_t) &argv[1]);
          if (err)
            error (1, err, "proc_set_arg_locations");
        }

      c = malloc (sizeof *c);
      if (c == NULL)
	error (1, errno, "malloc");
      c->task = task;
      c->next = children;
      children = c;

      err = vm_deallocate (mach_task_self (), address, (vm_size_t) s);
      if (err)
	error (1, err, "vm_deallocate");

      allocated += s;
      nchildren += 1;

      if (verbose)
        fprintf (stderr, "\rAllocated %"PRIu64" bytes.\n", allocated);
    }

  if (do_pause)
    {
      fprintf (stderr, "Press enter to exit and release the memory.");
      getchar ();
    }

  for (c = children; c; c = c->next)
    task_terminate (c->task);

  return EXIT_SUCCESS;
}
