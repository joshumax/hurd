/* Test filesystem behavior
   Copyright (C) 1993, 1994 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <mach.h>
#include <stdio.h>
#include <hurd.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

void
main ()
{
  int fd
  char pathbuf[1024];
  int written;
  
  stderr = stdout = mach_open_devstream (getdport (1), "w");

  if (unlink (root, "CREATED") < 0 && errno != ENOENT)
    printf ("Error on unlink: %d\n", errno);
  fd = open ("CREATED", O_WRITE | O_CREAT, 0666);
  if (fd < 0)
    printf ("Error on open: %d\n", errno);
  written = write (fd, string, strlen (string));
  if (written < 0)
    printf ("Error on write: %d\n", errno);
  else if (written != strlen (string))
    printf ("Short write: %d\n", written);
  else if (sync ())
    printf ("Error on sync: %d\n", errno);

  printf ("All done.\n");
  malloc (0);
}
