/* Test races in the record locking code.

   Copyright (C) 2001 Free Software Foundation, Inc.

   Written by Neal H Walfield <neal@cs.uml.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <error.h>
#include <fcntl.h>
#include <unistd.h>
#include "fs_U.h"
#include <hurd.h>
#include "io_U.h"

int main (int argc, char **argv)
{
  error_t err;
  struct flock64 lock;
  mach_port_t rendezvous = MACH_PORT_NULL;
  int fd;
  int i;
  uint v;
  int blocked = 0;
  char buf[10] = "";
  char *bufp;

  if (argc != 4)
    error (1, 0, "Usage: %s file start len", argv[0]);

  lock.l_whence = SEEK_SET;
  lock.l_start = atoi (argv[2]);
  lock.l_len = atoi (argv[3]);

  fd = file_name_lookup (argv[1], O_READ | O_WRITE | O_CREAT, 0666);
  if (fd == MACH_PORT_NULL)
    error (1, errno, "file_name_lookup");

  for (i = 0; i < 10000; i ++)
    {
      lock.l_type = F_WRLCK;
      err = file_record_lock (fd, F_SETLK64, &lock, rendezvous, MACH_MSG_TYPE_MAKE_SEND);
      if (err)
        {
	  blocked ++;
          err = file_record_lock (fd, F_SETLKW64, &lock, rendezvous, MACH_MSG_TYPE_MAKE_SEND);
	}
      if (err)
        error (1, err, "file_record_lock");

      v = sizeof (buf);
      bufp = buf;
      io_read (fd, &bufp, &v, 0, v);

      v = atoi (bufp);
      sprintf (buf, "%d\n", v + 1);

      v = 10;
      io_write (fd, buf, sizeof (buf), 0, &v);
      if (v == 0)
        error (1, errno, "write (%d)", i);

      lock.l_type = F_UNLCK;
      file_record_lock (fd, F_SETLK64, &lock, rendezvous, MACH_MSG_TYPE_MAKE_SEND);
    }

  mach_port_deallocate (mach_task_self (), fd);

  printf ("Was blocked %d times\n", blocked);
  return 0;
}
