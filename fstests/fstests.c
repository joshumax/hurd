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

/* Written by Michael I. Bushnell.  */

#include <mach.h>
#include <stdio.h>
#include <hurd/hurd_types.h>
#include <hurd/fs.h>
#include <hurd/io.h>
#include <hurd.h>
#include <fcntl.h>
#include <errno.h>

mach_port_t _hurd_msgport = MACH_PORT_NULL;

void
main ()
{
  file_t filetowrite;
  retry_type retry;
  char pathbuf[1024];
  extern file_t *_hurd_init_dtable;
  char string[] = "Did this get into the file?\n";
  int written;
  int err;
  mach_port_t root;
  
  root = _hurd_ports[INIT_PORT_CRDIR].port;

  printf ("fstests running...\n");

#if 0
  if ((err = dir_unlink (root, "CREATED")) && err != ENOENT)
    printf ("Error on unlink: %d\n", err);
  else if (err = dir_pathtrans (root, "CREATED", O_WRITE | O_CREAT, 0666,
				&retry, pathbuf, &filetowrite))
    printf ("Error on pathtrans: %d\n", err);
  else if (err = io_write (filetowrite, string, strlen (string), -1, &written))
    printf ("Error on write: %d\n", err);
  else if (written != strlen (string))
    printf ("Short write: %d\n", written);
  else if (err = file_syncfs (filetowrite, 1, 0))
    printf ("Error on sync: %d\n", err);
#else

  unlink ("/newdir");
  dir_unlink (root, "newdir");	/* unlink ("/newdir") */
  dir_rmdir (root, "newdir");	/* rmdir */
  dir_mkdir (root, "newdir", 0777);
  dir_rename (root, "newdir", root, "newdir2");
  dir_rmdir (root, "foo");
  dir_mkdir (root, "foo", 0777);
  dir_rename (root, "newdir2", root, "foo");
  file_syncfs (root, 1, 0);

#endif

  printf ("All done.\n");
  malloc (0);
}
