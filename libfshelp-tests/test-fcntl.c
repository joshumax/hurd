/* test-fcntl.c: Test advisory open file record locks, see fcntl(2)
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
		int *cmd, char **cmdc,
		struct flock *lock,
		char **l_typec, char **l_whencec,
		int *sleep_time)
{
  int i, tmp;
  char *str, *endptr;

  if (argc < 2)
    error (1, 0, "Usage: %s file [flags] [cmd] [len] [sleep_time]\n\
    file          : file name/device name\n\
    flags         : r (O_RDONLY) | w (O_WRONLY) | rw (O_RDWR)      : [rw]\n\
    cmd           : g (F_GETLK), s (F_SETLK), sw (F_SETLKW)        : [s]\n\
    lock.l_type   : rl (F_RDLCK), wl (F_WRLCK), ul [F_UNLCK]       : [ul]\n\
    lock.l_whence : ss (SEEK_SET), sc (SEEK_CUR), se (SEEK_END)    : [ss]\n\
    lock.l_start  : b <number>                                     : [b 0]\n\
    lock.l_len    : l <number>                                     : [l 0]\n\
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
      	  *cmd = F_SETLK;
	  *cmdc = "F_SETLK";
	  continue;
	}
      if (strncmp (str, "sw", 2) == 0)
	{
	  *cmd = F_SETLKW;
	  *cmdc = "F_SETLKW";
	  continue;
	}
      if (strncmp (str, "g", 2) == 0)
	{
	  *cmd = F_GETLK;
	  *cmdc = "F_GETLK";
	  continue;
	}
      if (strncmp (str, "rl", 2) == 0)
	{
	  lock->l_type = F_RDLCK;
	  *l_typec = "F_RDLCK";
	  continue;
	}
      if (strncmp (str, "wl", 2) == 0)
	{
	  lock->l_type = F_WRLCK;
	  *l_typec = "F_WRLCK";
	  continue;
	}
      if (strncmp (str, "ul", 2) == 0)
	{
	  lock->l_type = F_UNLCK;
	  *l_typec = "F_UNLCK";
	  continue;
	}
      if (strncmp (str, "ss", 2) == 0)
	{
	  lock->l_whence = SEEK_SET;
	  *l_whencec = "SEEK_SET";
	  continue;
	}
      if (strncmp (str, "sc", 2) == 0)
	{
	  lock->l_whence = SEEK_CUR;
	  *l_whencec = "SEEK_CUR";
	  continue;
	}
      if (strncmp (str, "se", 2) == 0)
	{
	  lock->l_whence = SEEK_END;
	  *l_whencec = "SEEK_END";
	  continue;
	}
      if (strncmp (str, "b", 2) == 0)
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
	      lock->l_start = tmp;
	    }
	  else
	    error (1, EINVAL, "%s", str);
	  continue;
	}
      if (strncmp (str, "l", 2) == 0)
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
	      lock->l_len = tmp;
	    }
	  else
	    error (1, EINVAL, "%s", str);
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
  int flags = O_RDWR;
  char *flagsc = "O_RDWR";
  char *old_l_typec;
  int old_l_type, old_l_pid;
  int cmd = F_SETLK;
  char *cmdc = "F_SETLK";
  struct flock lock = {
    F_UNLCK,
    SEEK_SET,
    0,
    0,
    123456};
  char *l_typec = "F_UNLCK";
  char *l_whencec = "SEEK_SET";
  int sleep_time = 10;

  ret = parse_args (argc, argv, &file_name,
		    &flags, &flagsc,
		    &cmd, &cmdc,
		    &lock,
		    &l_typec, &l_whencec,
		    &sleep_time);

#ifdef __GNU__
  printf ("test-fcntl: GNU/Hurd\n");
#else
  printf ("test-fcntl: GNU/Linux\n");
#endif
  printf ("test-fcntl: [PID]=%d\n", getpid());
  printf ("file = '%s', flags = %s\n", file_name, flagsc);
  fd = open (file_name, flags);
  if (fd < 0)
    error (1, errno, "open");
  printf ("Opening '%s', fd = %d, ", file_name, fd);
  printf ("cmd = %s\n ", cmdc);
  printf("lock = {l_type,  l_whence, l_start, l_len, l_pid} =\n");
#ifdef __GNU__
  printf ("        {%s, %s, %lld,       %lld,     %d}\n",
#else
  printf ("        {%s, %s, %ld,       %ld,     %d}\n",
#endif
         l_typec, l_whencec, lock.l_start, lock.l_len, lock.l_pid);

  old_l_type = lock.l_type;
  old_l_typec = l_typec;
  old_l_pid = lock.l_pid;

  printf ("Requesting lock\n");
  err = fcntl (fd, cmd, &lock);
  if (err)
    error (1, errno, "fcntl");

  if (old_l_type != lock.l_type)
    if (lock.l_type == F_UNLCK)
      {
	l_typec = "F_UNLCK";
	printf("[PID=%ld] Lock can be placed\n", (long) getpid());
	printf ("old_l_type = %s, l_type = %s\n", old_l_typec, l_typec);
	return ret;
      }
      if (old_l_pid != lock.l_pid)
    {
      printf("[PID=%ld] Denied by %s lock on %lld:%lld "
	     "(held by PID %ld)\n", (long) getpid(),
	     (lock.l_type == F_RDLCK) ? "READ" : "WRITE",
	     (long long) lock.l_start,
	     (long long) lock.l_len, (long) lock.l_pid);
      return ret;
    }
  printf ("Got lock: sleep_time = %d seconds\n", sleep_time);
  sleep (sleep_time);
  printf ("Closing '%s'\n", file_name);
  close (fd);

  return ret;
}
