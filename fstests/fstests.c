/* Test filesystem behavior
   Copyright (C) 1993,94,2000,01,02 Free Software Foundation, Inc.

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

/* Written by Michael I. Bushnell.  */

#include <mach.h>
#include <stdio.h>
#include <hurd/hurd_types.h>
#include <hurd/fs.h>
#include <hurd/io.h>
#include <hurd.h>
#include <fcntl.h>
#include <errno.h>
#include <error.h>
#include <unistd.h>
#include <stdlib.h>

int check_refs (mach_port_t port) /* To call from gdb */
{
  int err;
  mach_port_urefs_t refs;
  err = mach_port_get_refs (mach_task_self (),
			    port, MACH_PORT_RIGHT_SEND, &refs);
  return err ? -err : refs;
}

int
main ()
{
#if HURDISH_TESTS
  mach_port_t root;
  extern file_t *_hurd_init_dtable;
  char string[] = "Did this get into the file?\n";
  file_t filetowrite;
  retry_type retry;
  char pathbuf[1024];
  int written;
  error_t err;
#endif

#ifdef HURDISH_TESTS
  root = getcrdir ();
#else
  (void) getcrdir ();
#endif

  printf ("fstests running...\n");

#if HURDISH_TESTS
  if ((err = dir_unlink (root, "CREATED")) && err != ENOENT)
    error (0, err, "Error on unlink");
  else if (err = dir_lookup (root, "CREATED", O_WRITE | O_CREAT, 0666,
			     &retry, pathbuf, &filetowrite))
    error (0, err, "Error on lookup");
  else if (err = io_write (filetowrite, string, strlen (string), -1, &written))
    error (0, err, "Error on write");
  else if (written != strlen (string))
    error (0, 0, "Short write: %d\n", written);
  else if (err = file_syncfs (filetowrite, 1, 0))
    error (0, err, "Error on sync");
#else

  if (unlink ("/newdir"))
    error (0, errno, "unlink");
  if (rmdir ("/newdir"))
    error (0, errno, "1st rmdir");
  if (mkdir ("/newdir", 0777))
    error (0, errno, "1st mkdir");
  if (rename ("/newdir", "/newdir2"))
    error (0, errno, "1st rename");
  if (rmdir ("/foo"))
    error (0, errno, "2nd rmdir");
  if (mkdir ("/foo", 0777))
    error (0, errno, "2nd mkdir");
  if (rename ("/newdir2", "/foo"))
    error (0, errno, "2nd rename");
  sync ();
#endif

  printf ("All done.\n");
  malloc (0);

  return 0;
}
