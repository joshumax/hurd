/* Set options in a running filesystem

   Copyright (C) 1995 Free Software Foundation, Inc.

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
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>

#include <error.h>
#include <argz.h>

#include <hurd/fsys.h>

/* ---------------------------------------------------------------- */

#define USAGE "Usage: %s [OPTION...] FILESYS OPTIONS...\n" 

static void
usage(status)
     int status;
{
  if (status != 0)
    fprintf(stderr, "Try `%s --help' for more information.\n",
	      program_invocation_name);
  else
    {
      printf(USAGE, program_invocation_name);
      printf("\
\n\
  -L, --dereference          if FILE is a symbolic link, follow it\n\
  -R, --recursive            pass these options to any child translators\n\
      --help                 give this help list\n\
      --version              print version number of program\n\
");
    }

  exit(status);
}

#define SHORT_OPTIONS "LRV"

static struct option options[] =
{
  {"dereference", no_argument, 0, 'L'},
  {"recursive", no_argument, 0, 'R'},
  {"help", no_argument, 0, '&'},
  {"version", no_argument, 0, 'V'},
  {0, 0, 0, 0}
};

/* ---------------------------------------------------------------- */

void 
main(int argc, char *argv[])
{
  int opt;
  error_t err;

  /* The filesystem we're passing options to.  */
  fsys_t fsys;

  /* The file we use as a handle to get FSYS.  */
  char *node_name = 0;
  file_t node;

  /* The filesystem options vector, in '\0' separated format.  */
  char *argz = 0;
  int argz_len = 0;

  int deref = 0, recursive = 0;

  /* Parse our options...  */
  while ((opt = getopt_long(argc, argv, "-" SHORT_OPTIONS, options, 0)) != EOF)
    switch (opt)
      {
      case 1:
	node_name = optarg;
	err = argz_create(argv + optind, &argz, &argz_len);
	if (err)
	  error(3, err, "Can't create options vector");
	optind = argc;		/* stop parsing */
	break;
      case 'R': recursive = 1; break;
      case 'L': deref = 1; break;
      case 'V': printf ("%s 0.0\n", program_invocation_short_name); exit (0);
      case '&': usage(0);
      default:  usage(-1);
      }

  if (node_name == NULL)
    {
      fprintf (stderr, USAGE, program_invocation_short_name);
      usage (-1);
    }

  node = file_name_lookup(node_name, (deref ? 0 : O_NOLINK), 0666);
  if (node == MACH_PORT_NULL)
    error(1, errno, "%s", node_name);

  /* Get the filesystem for NODE.  */
  err = file_getcontrol (node, &fsys);
  if (err)
    error (2, err, "%s", node_name);

  err = fsys_set_options (fsys, argz, argz_len, recursive);
  if (err)
    {
      argz_stringify (argz, argz_len);
      error(5, err, "%s: %s", node_name, argz);
    }

  exit(0);
}
