/* Periodically call sync.
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

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>

int
main ()
{
  int fd;
  
  switch (fork ())
    {
    case -1:
      perror ("Cannot fork");
      exit (1);
    case 0:
      break;
    default:
      _exit (0);
    }
  
  if (setsid () == -1)
    {
      perror ("Cannot setsid");
      exit (1);
    }
  chdir ("/");
  
  fd = open ("/dev/null", O_RDWR, 0);
  if (fd != -1)
    {
      dup2 (fd, STDIN_FILENO);
      dup2 (fd, STDOUT_FILENO);
      dup2 (fd, STDERR_FILENO);
      if (fd < STDERR_FILENO)
	close (fd);
    }
  
  for (;;)
    {
      sync ();
      sleep (30);
    }
}
