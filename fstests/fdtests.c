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
#include <stdio.h>
#include <stdlib.h>
#include <hurd/fd.h>

int
main ()
{
  int fd;
  FILE *fp;
  static const char string[] = "Did this get into the file?\n";
  int written;
  
  setlinebuf (stdout);
  setlinebuf (stderr);

  if (unlink ("CREATED") < 0 && errno != ENOENT)
    printf ("Error on unlink: %d\n", errno);

  fd = open ("CREATED", O_WRITE | O_CREAT, 0666);
  if (fd < 0)
    printf ("Error on open: %d\n", errno);

  /* written = write (fd, string, strlen (string)); */

  {
    size_t nbytes = strlen (string);
    struct hurd_userlink __dt_ulink;
    error_t __result;
    struct hurd_fd_user __d = _hurd_fd_get (fd, &__dt_ulink);
    if (__d.d == NULL)
      __result = EBADF;
    else
      {
	struct hurd_fd *const descriptor = __d.d;
	__result = _hurd_fd_write (descriptor, string, &nbytes);
	_hurd_fd_free (__d, &__dt_ulink);
      }
    if (__result)
      errno = __result, written = -1;
    else
      written = nbytes;
  }

  if (written < 0)
    printf ("Error on write: %d\n", errno);
  else if (written != strlen (string))
    printf ("Short write: %d\n", written);
  else if (sync ())
    printf ("Error on sync: %d\n", errno);


  fp = fopen ("CREATED", "r");
  if (! fp)
    perror ("fopen");
  else
    {
      char *line = NULL;
      size_t len = 0;
      ssize_t n = getline (&line, &len, fp);
      if (n < 0)
	perror ("getline");
      else
	printf ("Read %d bytes: %.*s", n, n, line);
      free (line);
    }
  
  printf ("All done.\n");
  malloc (0);

  return 0;
}
