/* Periodically call sync.
   Copyright (C) 1994, 2002 Free Software Foundation, Inc.
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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int
main (int argc, char **argv)
{
  int interval;

  switch (argc)
    {
    case 1:
      interval = 30;
      break;
    case 2:
      interval = atoi (argv[1]);
      break;
    default:
      fprintf (stderr, "Usage: %s [SECONDS]\n", argv[0]);
      exit (1);
    }
  
  if (daemon (0, 0))
    error (1, errno, "daemon");

  for (;;)
    {
      sync ();
      sleep (interval);
    }
}
