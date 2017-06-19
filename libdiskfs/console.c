/* Redirect stdio to the console if possible

   Copyright (C) 1995,96,98,99,2001 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert-backtrace.h>

#include <mach/mach.h>
#include <device/device.h>
#include <hurd.h>

#include "priv.h"

/* Make sure errors go somewhere reasonable.  */
void
diskfs_console_stdio ()
{
  if (getpid () > 0)
    {
      if (write (2, "", 0) == 0)
	/* We have a working stderr from our parent (e.g. settrans -a).
	   Just use it.  */
	dup2 (2, 1);
      else
	{
	  int fd = open ("/dev/console", O_RDWR);

	  dup2 (fd, 0);
	  dup2 (fd, 1);
	  dup2 (fd, 2);
	  if (fd > 2)
	    close (fd);
	}
    }
  else
    {
      mach_port_t dev, cons;
      error_t err;
      if (diskfs_boot_filesystem ())
	_diskfs_boot_privports ();
      err = get_privileged_ports (NULL, &dev);
      assert_perror_backtrace (err);
      err = device_open (dev, D_READ|D_WRITE, "console", &cons);
      mach_port_deallocate (mach_task_self (), dev);
      assert_perror_backtrace (err);
      stdin = mach_open_devstream (cons, "r");
      stdout = stderr = mach_open_devstream (cons, "w");
      mach_port_deallocate (mach_task_self (), cons);
    }
}
