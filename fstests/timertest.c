/* A test for the Hurd timer and getchar
   Copyright (C) 1994,2001,02 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <error.h>

void
alarm_handler (int signo)
{
  printf ("Received alarm\n");
  fflush (stdout);
}

int
main(int argc, char *argv[])
{
  struct itimerval real_timer;

  real_timer.it_interval.tv_usec = 0;
  real_timer.it_interval.tv_sec = 1;
  real_timer.it_value.tv_usec = 0;
  real_timer.it_value.tv_sec = 1;

  signal (SIGALRM, alarm_handler);

  if (setitimer (ITIMER_REAL, &real_timer, 0) < 0)
    error (1, errno, "Setting timer");

  while (1)
    {
      int c;
      puts ("Pausing for input or one second...");
      fflush (stdout);
      c = getchar ();
      if (ferror (stdin))
	error (1, errno, "getchar");
      if (c == EOF)
	{
	  puts ("Saw EOF.  Pausing (no input)...");
	  fflush (stdout);
	  sigpause (0);
	}
      else
	printf ("Saw %.3o\n", c);
    }
}
