/* /etc/ttys support for Hurd init
   Copyright (C) 1993,94,95,96,97,98,99 Free Software Foundation, Inc.
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

#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <argz.h>
#include <hurd.h>
#include <error.h>
#include <assert.h>
#include <ttyent.h>
#include <utmp.h>

#include "ttys.h"


extern pid_t run_for_real (char *filename, char *args, int arglen,
			   mach_port_t ctty, int setsid); /* init.c */


/* How long to wait after starting window specs before starting getty */
#define WINDOW_DELAY 3		/* seconds */

#define _PATH_LOGIN "/bin/login"


/* All the ttys in /etc/ttys. */
struct terminal
{
  /* argz list for getty */
  char *getty_argz;
  size_t getty_argz_len;

  /* argz list for window spec */
  char *window_argz;
  size_t window_argz_len;

  int on;
  pid_t pid;
  int read;

  char *name;
};

static struct terminal *ttys;
/* Number of live elements in ttys */
static int nttys;
/* Total number of elements in ttys */
static int ttyslen;


/* Set up the getty and window fields of terminal spec T corresponding
   to line TT. */
static void
setup_terminal (struct terminal *t, struct ttyent *tt)
{
  char *line;

  if (t->getty_argz)
    free (t->getty_argz);
  if (t->window_argz)
    free (t->window_argz);

  if ((tt->ty_status & TTY_ON) && tt->ty_getty)
    {
      asprintf (&line, "%s %s", tt->ty_getty, tt->ty_name);
      argz_create_sep (line, ' ', &t->getty_argz, &t->getty_argz_len);
      free (line);
      if (tt->ty_window)
	argz_create_sep (tt->ty_window, ' ',
			 &t->window_argz, &t->window_argz_len);
      else
	t->window_argz = 0;
    }
  else
      t->getty_argz = t->window_argz = 0;
}


/* Add a new terminal spec for TT and return it. */
static struct terminal *
add_terminal (struct ttyent *tt)
{
  struct terminal *t;

  if (nttys >= ttyslen)
    {
      ttys = realloc (ttys, (ttyslen * 2) * sizeof (struct ttyent));
      bzero (&ttys[nttys], ttyslen);
      ttyslen *= 2;
    }

  t = &ttys[nttys];
  nttys++;

  t->name = malloc (strlen (tt->ty_name) + 1);
  strcpy (t->name, tt->ty_name);

  setup_terminal (t, tt);
  if (t->getty_argz)
    t->on = 1;

  return t;
}

/* Read /etc/ttys and initialize ttys array.  Return non-zero if we fail. */
int
init_ttys (void)
{
  struct ttyent *tt;

  ttyslen = 10;
  nttys = 0;

  ttys = malloc (ttyslen * sizeof (struct ttyent));
  bzero (ttys, ttyslen * sizeof (struct ttyent));

  if (!setttyent ())
    {
      error (0, errno, "%s", _PATH_TTYS);
      return 1;
    }
  while ((tt = getttyent ()))
    {
      if (!tt->ty_name)
	continue;

      add_terminal (tt);
    }

  endttyent ();
  return 0;
}

/* Free everyting in the terminal array */
void
free_ttys (void)
{
  int i;

  for (i = 0; i < nttys; i++)
    {
      if (ttys[i].getty_argz)
	free (ttys[i].getty_argz);
      if (ttys[i].window_argz)
	free (ttys[i].window_argz);
      free (ttys[i].name);
    }
  free (ttys);
}

/* Start line T.  Return non-zero if we didn't actually start anything.  */
static int
startup_terminal (struct terminal *t)
{
  pid_t pid;
  assert (t->on);
  assert (t->getty_argz);

  if (t->window_argz)
    {
      pid = run_for_real (t->window_argz, t->window_argz,
			  t->window_argz_len, MACH_PORT_NULL, 1);
      if (!pid)
	goto error;

      sleep (WINDOW_DELAY);
    }

  pid = run_for_real (t->getty_argz, t->getty_argz,
		      t->getty_argz_len, MACH_PORT_NULL, 0);
  if (pid == 0)
    {
    error:
      t->pid = 0;
      t->on = 0;
      return 1;
    }
  else
    {
      t->pid = pid;
      return 0;
    }
}

/* For each line in /etc/ttys, start up the specified program.  Return
   non-zero if we fail.  */
int
startup_ttys (void)
{
  int i;
  int didone, fail;

  didone = 0;

  for (i = 0; i < nttys; i++)
    if (ttys[i].on)
      {
	fail = startup_terminal (&ttys[i]);
	if (!fail)
	  didone = 1;
      }
  return !didone;
}

/* Find the terminal spec corresponding to line LINE. */
static struct terminal *
find_line (char *line)
{
  int i;

  for (i = 0; i < nttys; i++)
    if (!strcmp (ttys[i].name, line))
      return &ttys[i];
  return 0;
}

/* PID has just exited; restart the terminal it's on if necessary. */
void
restart_terminal (pid_t pid)
{
  int i;

  for (i = 0; i < nttys; i++)
    if (pid == ttys[i].pid)
      {
	if (logout (ttys[i].name))
	  logwtmp (ttys[i].name, "", "");
	ttys[i].pid = 0;
	if (ttys[i].on)
	  startup_terminal (&ttys[i]);
      }
}

/* Shutdown the things running on terminal spec T. */
static void
shutdown_terminal (struct terminal *t)
{
  kill (t->pid, SIGHUP);
  revoke (t->name);
}

/* Re-read /etc/ttys.  If a line has turned off, kill what's there.
   If a line has turned on, start it.  */
void
reread_ttys (void)
{
  struct ttyent *tt;
  struct terminal *t;
  int on;
  int i;

  if (!setttyent ())
    {
      error (0, errno, "%s", _PATH_TTYS);
      return;
    }

  /* Mark all the lines not yet read */
  for (i = 0; i < nttys; i++)
    ttys[i].read = 0;

  while ((tt = getttyent ()))
    {
      if (!tt->ty_name)
	continue;

      t = find_line (tt->ty_name);
      on = tt->ty_getty && (tt->ty_status & TTY_ON);

      if (t)
	{
	  if (t->on && !on)
	    {
	      t->on = 0;
	      shutdown_terminal (t);
	    }
	  else if (!t->on && on)
	    {
	      t->on = 1;
	      setup_terminal (t, tt);
	      startup_terminal (t);
	    }
	}
      else
	{
	  t = add_terminal (tt);
	  if (on)
	    startup_terminal (t);
	}

      t->read = 1;
    }
  endttyent ();

  /* Scan tty entries; any that were not found and were on, turn off. */
  for (i = 0; i < nttys; i++)
    if (!ttys[i].read && ttys[i].on)
      {
	ttys[i].on = 0;
	shutdown_terminal (&ttys[i]);
      }
}
