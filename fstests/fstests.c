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

main ()
{
  file_t root, passwd;
  retry_type retry;
  char buf[1024], *bp;
  char pathbuf[1024];
  
  stdout = mach_open_devstream (_hurd_init_dtable[1], "w");

  task_get_bootstrap_port (mach_task_self (), &root);
  dir_pathtrans (root, "etc/passwd", FS_LOOKUP_READ, 0, &retry, pathbuf,
		 &passwd);
  
  do
    {
      bp = buf;
      buflen = 10;
      io_read (passwd, &bp, &buflen, -1, 10);
      bp[buflen] = '\0';
      printf ("%s", bp);
      if (bp != buf)
	vm_deallocate (mach_task_self (), (vm_address_t) bp, buflen);
    }
  while (buflen);
  printf ("All done.\n");
}
