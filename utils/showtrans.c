/* Show files' passive translators.

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

/* ---------------------------------------------------------------- */

#define USAGE "Usage: %s [OPTION...] FILE...\n" 

#define SHORT_OPTIONS "hs?"

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
  -p, --prefix               always display `FILENAME: ' before translators\n\
  -P, --no-prefix            never display `FILENAME: ' before translators\n\
  -s, --silent               no output; useful when checking error status\n\
      --help                 give this help list\n\
");
    }

  exit(status);
}

static struct option options[] =
{
  {"prefix", no_argument, 0, 'p'},
  {"no-prefix", no_argument, 0, 'P'},
  {"silent", no_argument, 0, 's'},
  {"quiet", no_argument, 0, 's'},
  {"help", no_argument, 0, '&'},
  {0, 0, 0, 0}
};

/* ---------------------------------------------------------------- */

void 
main(int argc, char *argv[])
{
  int opt;
  /* The default exits status -- changed to 0 if we find any translators.  */
  int status = 1;
  /* Some option flags.  */
  int print_prefix = -1, silent = 0;

  /* Parse our options...  */
  while ((opt = getopt_long(argc, argv, "+" SHORT_OPTIONS, options, 0)) != EOF)
    switch (opt)
      {
      case 'p': print_prefix = 1; break;
      case 'P': print_prefix = 0; break;
      case 's': silent = 1; break;
      case '&': usage(0); break;
      default:  usage(-1); break;
      }

  if (print_prefix < 0)
    /* By default, only print a filename prefix if there are multiple files. */
    print_prefix = (argc > optind + 1);

  while (optind != argc)
    {
      char *node_name = argv[optind++];
      file_t node = file_name_lookup(node_name, O_NOTRANS, 0);

      if (node == MACH_PORT_NULL)
	error(0, errno, "%s", node_name);
      else
	{
	  char buf[1024], *trans = buf;
	  int trans_len = sizeof(buf);
	  error_t err = file_get_translator(node, &trans, &trans_len);

	  switch (err)
	    {
	    case 0:
	      /* Make the '\0's in TRANS printable.  */
	      argz_stringify(trans, trans_len);

	      if (!silent)
		if (print_prefix)
		  printf("%s: %s\n", node_name, trans);
		else
		  puts(trans);

	      if (trans != buf)
		vm_deallocate(mach_task_self(),
			      (vm_address_t)trans, trans_len);

	      status = 0;

	      break;

	    case EINVAL:
	      /* NODE just doesn't have a translator.  */
	      if (!silent && print_prefix)
		puts(node_name);
	      break;

	    default:
	      error(0, err, "%s", node_name);
	    }

	  mach_port_deallocate(mach_task_self(), node);
	}
    }

  exit(status);
}
