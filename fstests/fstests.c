/* Test filesystem behavior
   Copyright (C) 1993 Free Software Foundation

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

void
main ()
{
  file_t root, filetoprint;
  retry_type retry;
  char buf[1024], *bp;
  int buflen;
  char pathbuf[1024];
  extern file_t *_hurd_init_dtable;

  stdout = mach_open_devstream (_hurd_init_dtable[1], "w");

  root = _hurd_ports[INIT_PORT_CRDIR].port;
  task_get_bootstrap_port (mach_task_self (), &root);
  dir_pathtrans (root, "README", FS_LOOKUP_READ, 0, &retry, pathbuf,
		 &filetoprint);
  
  do
    {
      bp = buf;
      buflen = 10;
      io_read (filetoprint, &bp, &buflen, -1, 10);
      bp[buflen] = '\0';
      printf ("%s", bp);
      if (bp != buf)
	vm_deallocate (mach_task_self (), (vm_address_t) bp, buflen);
    }
  while (buflen);
  printf ("All done.\n");
  malloc (0);
}
