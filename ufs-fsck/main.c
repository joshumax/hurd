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

#include "fsck.h"

/* Pretty primitive, I'm afraid. */

int
main (int argc, char **argv)
{
  if (argc != 2)
    {
      fprintf (stderr, "Usage: %s device", argv[0]);
      exit (1);
    }
  
  preen = 0;
  
  if (!setup (argv[1]))
    exit (1);
  
  printf ("** Phase 1 -- Check Blocks and Sizes\n");
  pass1 ();
  
  if (duplist)
    {
      printf ("** Phase 1b -- Rescan for More Duplicates\n");
      pass1b ();
    }
  
  printf ("** Phase 2 -- Check Pathnames\n");
  pass2 ();
  
  printf ("** Phase 3 -- Check Connectivity\n");
  pass3 ();
  
  printf ("** Phase 4 -- Check Reference Counts\n");
  pass4 ();
  
  printf ("** Phase 5 -- Check Cyl Groups\n");
  pass5 ();
  
  if (fsmodified)
    printf ("\n***** FILE SYSTEM WAS MODIFIED *****\n");
  return 0;
}  
