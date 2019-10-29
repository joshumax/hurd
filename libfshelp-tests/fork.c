/* Test is a process inherits locks after a fork.

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

#include <assert.h>
#include <stdio.h>
#include <error.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include "fs_U.h"
#include <hurd.h>

char *lock2str (int type)
{
  if (type & LOCK_SH)
    return "read";
  if (type & LOCK_EX)
    return "write";
  if (type & LOCK_UN)
    return "unlocked";
 assert (! "Invalid");
 return NULL;
}

int main (int argc, char **argv)
{
  error_t err;
  struct flock64 lock;
  mach_port_t rendezvous = MACH_PORT_NULL;
  int fd;
  pid_t pid;
  int mine, others;

  if (argc != 2)
    error (1, 0, "Usage: %s file", argv[0]);

  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;
  lock.l_type = F_WRLCK;

  fd = file_name_lookup (argv[1], O_READ | O_WRITE | O_CREAT, 0666);
  if (fd == MACH_PORT_NULL)
    error (1, errno, "file_name_lookup");

  err = file_record_lock (fd, F_SETLK64, &lock, rendezvous, MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    error (1, err, "file_record_lock");

  pid = fork ();
  if (pid == -1)
    error (1, errno, "fork");

  err = file_lock_stat (fd, &mine, &others);
  if (err)
    error (1, err, "file_lock_stat");

  printf ("%s has a %s lock; Others have a %s lock.\n",
	  pid ? "Parent" : "Child", lock2str (mine), lock2str (others));

  mach_port_deallocate (mach_task_self (), fd);

  return 0;
}
