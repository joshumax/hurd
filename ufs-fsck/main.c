/* Main program for GNU fsck
   Copyright (C) 1994 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <errno.h>
#include <getopt.h>

#include "fsck.h"

char *lfname = "lost+found";
mode_t lfmode = 0755;

#define USAGE "Usage: %s [OPTION...] DEVICE\n"

static void
usage(int status)
{
  if (status != 0)
    fprintf(stderr, "Try `%s --help' for more information.\n",
	    program_invocation_name);
  else
    {
      printf(USAGE, program_invocation_name);
      printf("\
\n\
  -p, --preen                Terse automatic mode\n\
  -y, --yes                  Automatically answer yes to all questions\n\
  -n, --no                   Automatically answer no to all questions\n\
  -l, --lost+found=NAME      The name of the lost+found directory in /\n\
  -m, --lf-mode=MODE         The name of the lost+found directory in /\n\
      --help                 Give this usage message\n\
");
    }

  exit(status);
}

#define SHORT_OPTIONS "pynlm&"

static struct option options[] =
{
  {"preen", no_argument, 0, 'p'},
  {"yes", no_argument, 0, 'y'},
  {"no", no_argument, 0, 'n'},
  {"lost+found", required_argument, 0, 'l'},
  {"lf-mode", required_argument, 0, 'm'},
  {"help", no_argument, 0, '&'},
  {0, 0, 0, 0}
};

int
main (int argc, char **argv)
{
  int opt;

  preen = nowrite = noquery = 0;
  
  while ((opt = getopt_long (argc, argv, SHORT_OPTIONS, options, 0)) != EOF)
    switch (opt)
      {
      case 'p': preen = 1; break;
      case 'y': noquery = 1; break;
      case 'n': nowrite = 1; break;
      case 'l': lfname = optarg; break;
      case 'm': lfmode = strtol (optarg, 0, 8); break;
      case '&': usage(0);
      default:  usage(1);
      }

  if (argv[optind] == 0 || argv[optind + 1] != 0)
    {
      fprintf(stderr, USAGE, program_invocation_name);
      usage (1);
    }

  if (!setup (argv[optind]))
    exit (1);
  
  if (!preen)
    printf ("** Phase 1 -- Check Blocks and Sizes\n");
  pass1 ();
  
  if (duplist)
    {
      if (!preen)
	printf ("** Phase 1b -- Rescan for More Duplicates\n");
      pass1b ();
    }
  
  if (!preen)
    printf ("** Phase 2 -- Check Pathnames\n");
  pass2 ();
  
  if (!preen)
    printf ("** Phase 3 -- Check Connectivity\n");
  pass3 ();
  
  if (!preen)
    printf ("** Phase 4 -- Check Reference Counts\n");
  pass4 ();
  
  if (!preen)
    printf ("** Phase 5 -- Check Cyl Groups\n");
  pass5 ();
  
  if (fsmodified)
    printf ("\n***** FILE SYSTEM WAS MODIFIED *****\n");
  return 0;
}  
