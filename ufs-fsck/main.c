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
#include <hurd.h>

#include "fsck.h"

char *argp_program_version = "fsck.ufs 1.0 (GNU " HURD_RELEASE ")";

char *lfname = "lost+found";
mode_t lfmode = 0755;

/* Terse automatic mode for noninteractive use; punts on severe problems.  */
int preen = 0;

/* Total number of files found on the partition.  */
long num_files = 0;

static struct argp_option options[] =
{
  {"preen",      'p', 0,      0,  "Terse automatic mode", 1},
  {"yes",        'y', 0,      0,  "Automatically answer yes to all questions"},
  {"no",         'n', 0,      0,  "Automatically answer no to all questions"},
  {"lost+found", 'l', "NAME", 0,  "The name of the lost+found directory in /"},
  {"lf-mode",    'm', "MODE", 0,  "The mode of the lost+found directory in /"},
  {0, 0, 0, 0, "In --preen mode, the following also apply:", 2},
  {"force",	 'f', 0,      0,  "Check even if clean"},
  {"silent",     's', 0,      0,  "Only print diagostic messages"},
  {0, 0}
};
char *args_doc = "DEVICE";

/* Returns a malloced buffer containing a nice printable size for FRAGS.  */
static char *
nice_size (long frags)
{
  char *rep;
  char *units = "KMGT", *u = units;
  float num = ((float)frags * sblock->fs_fsize) / 1024;

  while (num > 1024)
    {
      num /= 1024;
      u++;
    }

  asprintf (&rep, num >= 1000 ? "%.0f%c" : "%.3g%c", num, *u);

  return rep;
}

/* Print summary statistics.  */
static void
show_stats ()
{
  long num_ffree = sblock->fs_cstotal.cs_nffree;
  long num_bfree = sblock->fs_cstotal.cs_nbfree;
  long tot_ffree = num_ffree + sblock->fs_frag * num_bfree;
  char *urep = nice_size (sblock->fs_dsize - tot_ffree);
  char *frep = nice_size (tot_ffree);
  warning (0, "%ld files, %s used, %s free (%ld.%ld%% fragmentation)",
	   num_files, urep, frep,
	   (num_ffree * 100) / sblock->fs_dsize,
	   (((num_ffree * 1000 + sblock->fs_dsize / 2) / sblock->fs_dsize)
	    % 10));
  free (urep);
  free (frep);
}

int
main (int argc, char **argv)
{
  int silent = 0, force = 0;
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
	case 'f': force = 1; break;
	case 's': silent = 1; break;
	case ARGP_KEY_ARG:
	  if (!device)
	    {
	      device = arg;
	      break;
	    }
	  /* Fall through */
	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);
	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc};

  preen = nowrite = noquery = 0;
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (!setup (device))
    exit (1);
  
  if (preen && sblock->fs_clean && !force)
    {
      if (! silent)
	warning (0, "FILESYSTEM CLEAN");
    }
  else
    {
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
      
      if (! silent)
	show_stats (sblock);
    }

  if (fsmodified && !preen)
    printf ("\n***** FILE SYSTEM WAS MODIFIED *****\n");

  exit (fsmodified ? 2 : 0);
}
