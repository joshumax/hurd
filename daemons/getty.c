/* Stubby version of getty for Hurd

   Copyright (C) 1996, 1998, 1999, 2007 Free Software Foundation, Inc.

   Written by Michael I. Bushnell, p/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <syslog.h>
#include <unistd.h>
#include <ttyent.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <error.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <string.h>
#include <utmp.h>
#include <sys/ioctl.h>
#include <termios.h>

/* XXX */
extern char *localhost ();

#define _PATH_LOGIN "/bin/login"

/* Parse the terminal speed.  */
static void
set_speed (int tty, char *speedstr)
{
  error_t err;
  struct termios ttystat;
  speed_t speed;
  char *tail;

  errno = 0;
  speed = strtoul (speedstr, &tail, 0);
  if (errno || *tail)
    return;

  err = tcgetattr (tty, &ttystat);
  if (!err && !cfsetspeed (&ttystat, speed))
    tcsetattr (tty, TCSAFLUSH, &ttystat);
}


/* Print a suitable welcome banner */
static void
print_banner (int fd, char *ttyname)
{
  int cc;
  char *s;
  struct utsname u;
  char *hostname = localhost ();

  if (uname (&u))
    u.sysname[0] = u.release[0] = '\0';

  cc = asprintf (&s, "\r\n\n%s %s (%s) (%s)\r\n\n",
		 u.sysname, u.release, hostname ?: "?", basename (ttyname));
  write (fd, s, cc);
}

int
main (int argc, char **argv)
{
  char *linespec, *ttyname;
  int tty;
  struct ttyent *tt;
  char *arg;

  openlog ("getty", LOG_ODELAY|LOG_CONS|LOG_PID, LOG_AUTH);

  /* Nothing to do .... */
  if (argc != 3)
    {
      syslog (LOG_ERR, "Bad syntax");
      closelog ();
      exit (1);
    }

  /* Don't do anything with this for now. */
  linespec = argv[1];

  tt = getttynam (argv[2]);
  asprintf (&ttyname, "%s/%s", _PATH_DEV, argv[2]);

  chown (ttyname, 0, 0);
  chmod (ttyname, 0600);
  revoke (ttyname);
  sleep (2);			/* leave DTR down for a bit */

  do
    {
      tty = open (ttyname, O_RDWR);
      if (tty == -1)
	{
	  syslog (LOG_ERR, "%s: %m", ttyname);
	  closelog ();
	  sleep (60);
	}
    }
  while (tty == -1);

  set_speed (tty, linespec);

  print_banner (tty, ttyname);

  if (login_tty (tty) == -1)
    syslog (LOG_ERR, "cannot set controlling terminal to %s: %m", ttyname);

  asprintf (&arg, "TERM=%s", tt ? tt->ty_type : "unknown");

  if (tt && strcmp (tt->ty_type, "dialup") == 0)
    /* Dialup lines time out (which is login's default).  */
    execl (_PATH_LOGIN, "login", "-e", arg, NULL);
  else
    /* Hardwired lines don't.  */
    execl (_PATH_LOGIN, "login", "-e", arg, "-aNOAUTH_TIMEOUT", NULL);

  syslog (LOG_ERR, "%s: %m", _PATH_LOGIN);

  return 1;
}
