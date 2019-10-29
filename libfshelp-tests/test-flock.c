/* test-flock.c: Test advisory open file locks, see flock(2)
   Options: <see below>

   Copyright (C) 2016-2019 Free Software Foundation, Inc.

   Written by Svante Signell <svante.signell@gmail.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 3, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <sys/file.h>

/* Parse args */
int parse_args (int argc, char **argv, char **file_name,
		int *flags, char **flagsc,
		int *op, char **opc,
		int *sleep_time)
{
  int i, tmp;
  char *str, *endptr;

  if (argc < 2)
    error (1, 0, "Usage: %s file [flags] [operation] [sleep_time]\n\
    file          : file name/device name\n\
    flags         : r (O_RDONLY) | w (O_WRONLY) | rw (O_RDWR)      : [w]\n\
    operation     : s (LOCK_SH), x (LOCK_EX), u (LOCK_UN),\n\
                    sn (LOCK_SH | LOCK_UN), xn (LOCK_EX | LOCK_UN) : [s]\n\
    sleep_time    : st <number>                                    : [st 10]\n",
    argv[0]);

  *file_name = argv[1];
  for (i = 2; i < argc; i++)
    {
      str = argv[i];
      if (strncmp (str, "r", 2) == 0)
	{
	  *flags = O_RDONLY;
	  *flagsc = "O_RDONLY";
	  continue;
	}
      if (strncmp (str, "w", 2) == 0)
	{
	  *flags = O_WRONLY;
	  *flagsc = "O_WRONLY";
	  continue;
	}
      if (strncmp (str, "rw", 2) == 0)
	{
	  *flags = O_RDWR;
	  *flagsc = "O_RDWR";
	  continue;
	}
      if (strncmp (str, "s", 2) == 0)
	{
      	  *op = LOCK_SH;
	  *opc = "LOCK_SH";
	  continue;
	}
      if (strncmp (str, "sn", 2) == 0)
	{
	  *op = LOCK_SH | LOCK_NB;
	  *opc = "LOCK_SH | LOCK_NB";
	  continue;
	}
      if (strncmp (str, "x", 2) == 0)
	{
	  *op = LOCK_EX;
	  *opc = "LOCK_EX";
	  continue;
	}
      if (strncmp (str, "xn", 2) == 0)
	{
	  *op = LOCK_EX | LOCK_NB;
	  *opc = "LOCK_EX | LOCK_NB";
	  continue;
	}
      if (strncmp (str, "u", 2) == 0)
	{
	  *op = LOCK_UN;
	  *opc = "LOCK_UN";
	  continue;
	}
      if (strncmp (str, "st", 2) == 0)
	{
	  str = argv[++i];
	  if (str)
	    {
	      errno = 0;
	      tmp = strtol (str, &endptr, 10);
	      if (tmp == 0 && errno != 0)
		error (1, errno, "%s", str);
	      if (endptr == str)
		error (1, EINVAL, "%s", str);
	      *sleep_time = tmp;
	    }
	  else
	    error (1, EINVAL, "%s", str);
	  continue;
	}
      error (1, EINVAL, "%s", str);
    }

  return 0;
}

int main (int argc, char **argv)
{
#ifdef __GNU__
  error_t err;
#else
  int err;
#endif
  int fd, ret = -1;
  char *file_name = NULL;
  int flags = O_RDONLY;
  char *flagsc = "O_RDONLY";
  int op = LOCK_SH;
  char *opc = "LOCK_SH";
  int sleep_time = 10;

  ret = parse_args (argc, argv, &file_name,
		    &flags, &flagsc,
		    &op, &opc,
		    &sleep_time);

#ifdef __GNU__
  printf ("test-flock: GNU/Hurd\n");
#else
  printf ("test-flock: GNU/Linux\n");
#endif

  printf ("file = '%s', flags = %s\n", file_name, flagsc);
  fd = open (file_name, flags);
  if (fd < 0)
    error (1, errno, "open");
  printf ("Opening '%s', fd = %d, ", file_name, fd);
  printf ("operation = %s\n", opc);
  printf ("Requesting lock\n");
  err = flock (fd, op);
  if (err)
    error (1, errno, "flock");

  printf ("Got lock: sleep_time = %d seconds\n", sleep_time);
  sleep (sleep_time);

  printf ("Closing '%s'\n", file_name);
  close (fd);

  return ret;
}
