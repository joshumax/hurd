/* Set a file's translator.

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
#include <start-trans.h>

/* ---------------------------------------------------------------- */

#define USAGE "Usage: %s [OPTION...] FILE [TRANSLATOR...]\n" 

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
  -a, --active               Set FILE's active translator\n\
  -p, --passive              Set FILE's passive translator\n\
  -c, --create               Create FILE if it doesn't exist\n\
  -f, --force                Set the translator even if one already exists\n\
  -k, --keep-active          Keep any currently running active translator\n\
                             when setting the passive translator\n\
  -L, --dereference          If a translator exists, put the new one on top\n\
      --help                 Give this help list\n\
");
    }

  exit(status);
}

#define SHORT_OPTIONS "apcfkLt?"

static struct option options[] =
{
  {"active", no_argument, 0, 'a'},
  {"passive", no_argument, 0, 'p'},
  {"keep-active", no_argument, 0, 'k'},
  {"create", no_argument, 0, 'c'},
  {"force", no_argument, 0, 'f'},
  {"dereference", no_argument, 0, 'L'},
  {"help", no_argument, 0, '?'},
  {0, 0, 0, 0}
};

/* ---------------------------------------------------------------- */

void 
main(int argc, char *argv[])
{
  int opt;
  error_t err;

  /* The filesystem node we're putting a translator on.  */
  char *node_name = 0;
  file_t node;

  /* The translator's arg vector, in '\0' separated format.  */
  char *argz = 0;
  int argz_len = 0;

  /* The control port for any active translator we start up.  */
  fsys_t active_control = MACH_PORT_NULL;

  /* Flags to pass to file_set_translator.  By default we only set a
     translator if there's no existing one.  */
  int flags = FS_TRANS_SET | FS_TRANS_EXCL;

  /* Various option flags.  */
  int passive = 0, active = 0;
  int create = 0, force = 0, deref = 0, keep_active = 0;

  /* Parse our options...  */
  while ((opt = getopt_long(argc, argv, "-" SHORT_OPTIONS, options, 0)) != EOF)
    switch (opt)
      {
      case 1:
	if (node_name)
	  /* We've already read the node name, this must be the translator.  */
	  {
	    err = argz_create(argv + optind - 1, &argz, &argz_len);
	    if (err)
	      error(3, err, "Can't create argz vector");
	    optind = argc;	/* stop parsing */
	  }
	else
	  node_name = optarg;
	break;
      case 'a': active = 1; break;
      case 'p': passive = 1; break;
      case 'f': force = 1; break;
      case 'k': keep_active = 1; break;
      case 'c': create = 1; break;
      case 'L': deref = 1; break;
      case '?': usage(0);
      default:  usage(-1);
      }

  if (! node_name)
    usage (-1);

  if (!active && !passive)
    passive = 1;

  node =
    file_name_lookup(node_name,
		     (deref ? 0 : O_NOTRANS) | (create ? O_CREAT : 0),
		     0666);
  if (node == MACH_PORT_NULL)
    error(1, errno, "%s", node_name);

  if (active && argz_len > 0)
    {
      err = start_translator(node, argz, argz_len, 60000, &active_control);
      if (err)
	error(4, err, "%s", argz);
    }

  if (force)
    /* Kill any existing translators.  */
    flags &= ~FS_TRANS_EXCL;

  err =
    file_set_translator(node,
			passive ? flags : 0,
			(active || !keep_active) ? flags : 0,
			0,
			argz, argz_len,
			active_control, MACH_MSG_TYPE_MOVE_SEND);
  if (err)
    error(5, err, "%s", node_name);

  exit(0);
}
