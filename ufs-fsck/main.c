/* Main program for GNU fsck
   Copyright (C) 1994, 1996 Free Software Foundation, Inc.
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
#include <argp.h>

#include "fsck.h"

char *lfname = "lost+found";
mode_t lfmode = 0755;

static struct argp_option options[] =
{
  {"preen",      'p', 0,      0,  "Terse automatic mode"},
  {"yes",        'y', 0,      0,  "Automatically answer yes to all questions"},
  {"no",         'n', 0,      0,  "Automatically answer no to all questions"},
  {"lost+found", 'l', "NAME", 0,  "The name of the lost+found directory in /"},
  {"lf-mode",    'm', "MODE", 0,  "The mode of the lost+found directory in /"},
  {0, 0}
};
char *args_doc = "DEVICE";

int
main (int argc, char **argv)
{
  char *device = 0;
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'p': preen = 1; break;
	case 'y': noquery = 1; break;
	case 'n': nowrite = 1; break;
	case 'l': lfname = arg; break;
	case 'm': lfmode = strtol (arg, 0, 8); break;
	case ARGP_KEY_ARG:
	  if (!device)
	    {
	      device = arg;
	      break;
	    }
	  /* Fall through */
	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);
	default:  return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc};

  preen = nowrite = noquery = 0;
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (!setup (device))
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
  
  if (fsmodified && !preen)
    printf ("\n***** FILE SYSTEM WAS MODIFIED *****\n");

  exit (fsmodified ? 2 : 0);
}
