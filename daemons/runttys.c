/* /etc/ttys support for Hurd
   Copyright (C) 1993,94,95,96,97,98,99,2001 Free Software Foundation, Inc.
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

#include <argz.h>
#include <assert-backtrace.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ttyent.h>
#include <unistd.h>
#include <utmp.h>


/* How long to wait after starting window specs before starting getty */
#define WINDOW_DELAY 3		/* seconds */

#define _PATH_LOGIN "/bin/login"


/* All the ttys in /etc/ttys. */
struct terminal
{
  char *name;			/* Name of the terminal device file.  */

  /* argv lists for getty and window spec.
     The first element is always the malloc'd argz the rest point into.  */
  char **getty_argv, **window_argv;

  int on;			/* Nonzero iff the line is "on".  */
  pid_t pid;			/* Child running on this line.  */
  int read;			/* Used during reread_ttys.  */
};

static struct terminal *ttys;
/* Number of live elements in ttys */
static int nttys;
/* Total number of elements in ttys */
static int ttyslen;


static void
free_argvs (struct terminal *t)
{
  if (t->getty_argv)
    {
      free (t->getty_argv[0]);
      free (t->getty_argv);
    }
  if (t->window_argv)
    {
      free (t->window_argv[0]);
      free (t->window_argv);
    }
}

/* Set up the getty and window fields of terminal spec T corresponding
   to line TT. */
static void
setup_terminal (struct terminal *t, struct ttyent *tt)
{
  free_argvs (t);

  if ((tt->ty_status & TTY_ON) && tt->ty_getty)
    {
      char **make_args (const char *line)
	{
	  int argc;
	  char *argz, **argv;
	  size_t len;
	  argz_create_sep (line, ' ', &argz, &len);
	  argc = argz_count (argz, len);
	  argv = malloc ((argc + 1) * sizeof (char *));
	  if (argv == 0)
	    error (0, ENOMEM,
		   "cannot allocate argument vector for %s", t->name);
	  else
	    argz_extract (argz, len, argv);
	  return argv;
	}

      char *line;
      asprintf (&line, "%s %s", tt->ty_getty, tt->ty_name);
      if (line == 0)
	{
	  error (0, ENOMEM,
		 "cannot allocate arguments for %s", t->name);
	  t->getty_argv = 0;
	}
      else
	{
	  t->getty_argv = make_args (line);
	  free (line);
	}
      t->window_argv = tt->ty_window ? make_args (tt->ty_window) : 0;
    }
  else
      t->getty_argv = t->window_argv = 0;
}


/* Add a new terminal spec for TT and return it. */
static struct terminal *
add_terminal (struct ttyent *tt)
{
  struct terminal *t;

  if (nttys >= ttyslen)
    {
      struct terminal *newttys = realloc (ttys,
					  (ttyslen * 2) * sizeof ttys[0]);
      if (newttys == 0)
	{
	  error (0, ENOMEM, "cannot expand terminals table past %d", ttyslen);
	  return 0;
	}
      else
	{
	  ttys = newttys;
	  memset (&ttys[nttys], 0, ttyslen);
	  ttyslen *= 2;
	}
    }

  t = &ttys[nttys];
  t->name = strdup (tt->ty_name);
  if (t->name == 0)
    {
      error (0, ENOMEM, "cannot allocate entry for %s", tt->ty_name);
      return 0;
    }

  nttys++;
  setup_terminal (t, tt);
  if (t->getty_argv)
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

  ttys = calloc (ttyslen, sizeof ttys[0]);
  if (ttys == 0)
    error (2, ENOMEM, "cannot allocate table");

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

/* Free everything in the terminal array */
void
free_ttys (void)
{
  int i;

  for (i = 0; i < nttys; i++)
    {
      free_argvs (&ttys[i]);
      free (ttys[i].name);
    }
  free (ttys);
}

/* Start a child process.  */
static pid_t
run (char **argv, int do_setsid)
{
  pid_t pid;

  pid = fork ();
  if (pid < 0)
    {
      error (0, errno, "fork");
      return 0;
    }

  if (pid > 0)
    return pid;
  else
    {
      if (do_setsid && setsid () == -1)
	error (0, errno, "setsid");

      errno = 0;
      execv (argv[0], argv);
      error (127, errno, "%s", argv[0]);
    }

  /* NOTREACHED */
  return -1;
}


/* Start line T.  Return non-zero if we didn't actually start anything.  */
static int
startup_terminal (struct terminal *t)
{
  pid_t pid;
  assert_backtrace (t->on);
  assert_backtrace (t->getty_argv);

  if (t->window_argv)
    {
      pid = run (t->window_argv, 1);
      if (!pid)
	goto error;

      sleep (WINDOW_DELAY);
    }

  pid = run (t->getty_argv, 0);
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
	  if (t == 0)
	    continue;
	  if (on)
	    startup_terminal (t);
	}

      t->read = 1;
    }
  endttyent ();

  /* Scan tty entries; any that were not found and were on, turn off. */
  for (i = 0; i < nttys; i++)
    {
      if (!ttys[i].read && ttys[i].on)
	{
	  ttys[i].on = 0;
	  shutdown_terminal (&ttys[i]);
	}
      ttys[i].read = 0;		/* Clear flag for next time.  */
    }
}



/** Main program and signal handlers.  **/

static sig_atomic_t pending_hup;
static void
sighup (int signo)
{
  pending_hup = 1;
}

static sig_atomic_t pending_term;
static void
sigterm (int signo)
{
  pending_term = 1;
}

#ifdef SIGLOST
static void
reopen_console (int signo)
{
  int fd;

  close (0);
  close (1);
  close (2);

  fd = open (_PATH_CONSOLE, O_RDWR);
  if (fd < 0)
    _exit (2);
  if (fd != 0)
    {
      dup2 (fd, 0);
      close (fd);
    }
  dup2 (0, 1);
  dup2 (0, 2);
}
#endif

int
main ()
{
  int fail;
  struct sigaction sa;

  fail = init_ttys ();
  if (fail)
    return fail;

  if (setsid () == -1)
    error (0, errno, "setsid");

  sa.sa_handler = sighup;
  sa.sa_flags = 0;		/* No SA_RESTART! */
  sigemptyset(&sa.sa_mask);
  if (sigaction (SIGHUP, &sa, NULL))
    error (2, errno, "cannot set SIGHUP handler");
  sa.sa_handler = sigterm;
  if (sigaction (SIGTERM, &sa, NULL))
    error (2, errno, "cannot set SIGTERM handler");

#ifdef SIGLOST
  /* We may generate SIGLOST signal from trying to talk to the console
     after our port has been revoked or the term server has died.  In that
     case, reopen the console and restart.  (Unfortunately this won't
     restart the offending RPC on the new console port.)  */
  if (signal (SIGLOST, reopen_console) == SIG_ERR)
    error (2, errno, "cannot set SIGLOST handler");
#endif

  /* Start up tty lines.  */
  startup_ttys ();

  /* We will spend the rest of our life waiting for children to die.  */
  while (1)
    {
      error_t waiterr;
      pid_t pid = waitpid (WAIT_ANY, NULL, WUNTRACED);
      waiterr = errno;

      /* Elicit a SIGLOST now if the console (on our stderr, i.e. fd 2) has
	 died.  That way, the next error message emitted will actually make
	 it out to the console if it can be made it work at all.  */
      write (2, "", 0);

      /* If a SIGTERM or SIGHUP arrived recently, it set a flag
	 and broke us out of being blocked in waitpid.  */

      if (pending_term)
	{
	  pending_term = 0;
	  error (3, 0, "Got SIGTERM");
	}
      if (pending_hup)
	{
	  pending_hup = 0;
	  reread_ttys ();
	}

      if (pid < 0)
	{
	  if (waiterr == EINTR)	/* A signal woke us.  */
	    continue;
	  error (1, waiterr, "waitpid");
	}

      assert_backtrace (pid > 0);

      /* We have reaped a dead child.  Restart that tty line.  */
      restart_terminal (pid);
    }
}
