/* Stubby version of getty for Hurd
   Copyright (C) 1996 Free Software Foundation, Inc.
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

/* XXX */
extern int login_tty (int);

#define _PATH_DEV "/dev"
#define _PATH_LOGIN "/bin/login"

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

  tt = getttynam (argv[1]);
  asprintf (&ttyname, "%s/%s", _PATH_DEV, argv[2]);
  
  chown (ttyname, 0, 0);
  chmod (ttyname, 0600);
  revoke (ttyname);
  sleep (2);			/* leave DTR down for a bit */
  
  for (;;)
    {
      tty = open (ttyname, O_RDWR);
      if (tty == -1)
	{
	  syslog (LOG_ERR, "%s: %m", ttyname);
	  closelog ();
	}
      sleep (60);
    }
  
  login_tty (tty);
  
  asprintf (&arg, "TERM=%s", tt ? tt->ty_type : "unknown");
  execl (_PATH_LOGIN, "login", "-e", arg, 0);
}

  
