/*
   Copyright (C) 1994, 1995, 1999, 2002, 2010 Free Software Foundation

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

#include <sys/types.h>
#include <hurd.h>
#include <stdio.h>
#include <string.h>
#include <assert-backtrace.h>
#include <device/device.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <hurd/exec.h>

mach_port_t proc;
int pause_startup = 0;

void
reap (pid_t waitfor)
{
  pid_t pid;
  int status;

  while (1)
    {
      pid = waitpid (WAIT_ANY, &status, WUNTRACED | (waitfor ? 0 : WNOHANG));

      if (pid == -1)
	{
	  if (errno != ECHILD && errno != EWOULDBLOCK)
	    error (0, errno, "waitpid");
	  return;
	}
      else if (WIFEXITED (status))
	printf ("PID %d exit status %d\n",
		pid, WEXITSTATUS (status));
      else if (WIFSIGNALED (status))
	printf ("PID %d %s\n",
		pid, strsignal (WTERMSIG (status)));
      else if (WIFSTOPPED (status))
	printf ("PID %d stopped: %s\n",
		pid, strsignal (WSTOPSIG (status)));
      else
	printf ("PID %d bizarre status %#x\n", pid, status);

      if (pid == waitfor)
	waitfor = 0;
    }
}

pid_t
run (char **argv, int fd0, int fd1)
{
  file_t file;
  char *program;
  error_t err;

  if (strchr (argv[0], '/') != NULL)
    program = argv[0];
  else
    {
      size_t len = strlen (argv[0]);
      const char bin[] = "/bin/";
      program = alloca (sizeof bin + len);
      memcpy (program, bin, sizeof bin - 1);
      memcpy (&program[sizeof bin - 1], argv[0], len + 1);
    }

  file = file_name_lookup (program, O_EXEC, 0);
  if (file == MACH_PORT_NULL)
    {
      error (0, errno, "%s", program);
      return -1;
    }
  else
    {
      task_t task;
      pid_t pid;

      err = task_create (mach_task_self (),
#ifdef KERN_INVALID_LEDGER
			 NULL, 0,	/* OSF Mach */
#endif
			 0, &task);
      if (err)
	{
	  error (0, err, "task_create");
	  pid = -1;
	}
      else
	{
	  int save0 = -1;
	  int save1;

	  inline int movefd (int from, int to, int *save)
	    {
	      if (from == to)
		return 0;
	      *save = dup (to);
	      if (*save < 0)
		{
		  error (0, errno, "dup");
		  return -1;
		}
	      if (dup2 (from, to) != to)
		{
		  error (0, errno, "dup2");
		  return -1;
		}
	      close (from);
	      return 0;
	    }
	  inline int restorefd (int from, int to, int *save)
	    {
	      if (from == to)
		return 0;
	      if (dup2 (*save, to) != to)
		{
		  error (0, errno, "dup2");
		  return -1;
		}
	      close (*save);
	      return 0;
	    }

	  pid = task2pid (task);
	  if (pid == -1)
	    {
	      error (0, errno, "task2pid");
	      pid = 0;
	    }
	  err = proc_child (proc, task);
	  if (err)
	    error (0, err, "proc_child");
	  if (pause_startup)
	    {
	      printf ("Pausing (child PID %d)...", pid);
	      fflush (stdout);
	      getchar ();
	    }

	  if (movefd (fd0, 0, &save0) ||
	      movefd (fd1, 1, &save1))
	    return -1;

#ifdef HAVE__HURD_EXEC_PATHS
	  err = _hurd_exec_paths (task, file, program, program, argv, environ);
#else
	  err = _hurd_exec (task, file, argv, environ);
#endif
	  if (restorefd (fd0, 0, &save0) ||
	      restorefd (fd1, 1, &save1))
	    return -1;

	  if (err)
	    {
	      error (0, err, "_hurd_exec_paths");
	      err = task_terminate (task);
	      if (err)
		error (0, err, "task_terminate");
	    }
	  mach_port_deallocate (mach_task_self (), task);

	}
      mach_port_deallocate (mach_task_self (), file);

      errno = err;
      return pid;
    }
}

void
command (int argc, char **argv)
{
  pid_t pid;
  int bg;
  int i, start;
  int fds[2] = { 0, 1 };

  bg = !strcmp (argv[argc - 1], "&");
  if (bg)
    argv[--argc] = NULL;

  start = 0;
  for (i = 1; i < argc; ++i)
    if (! strcmp (argv[i], "|"))
      {
	int fd0 = fds[0];
	argv[i] = NULL;
	if (pipe (fds))
	  {
	    error (0, errno, "pipe");
	    return;
	  }
	pid = run (argv + start, fd0, fds[1]);
	if (pid < 0)
	  return;
	start = i + 1;
      }

  pid = run (argv + start, fds[0], 1);

  if (fds[0] != 0)
    close (fds[0]);
  if (fds[1] != 1)
    close (fds[1]);

  reap (bg ? 0 : pid);
}


int
main (int argc, char *argv[])
{
  char *linebuf = NULL;
  size_t linebufsize = 0;

  proc = getproc ();
  assert_backtrace (proc);

#if 0
  {
    error_t err;
    mach_port_t outp;
    mach_port_t hostp, masterd;
    err = proc_getprivports (proc, &hostp, &masterd);
    assert_backtrace (!err);

    err = device_open (masterd, D_WRITE|D_READ, "console", &outp);
    assert_backtrace (!err);

    stdin = mach_open_devstream (outp, "r");
    stdout = stderr = mach_open_devstream (outp, "w+");
  }
#endif

  /* Kludge to give boot a port to the auth server.  */
  exec_init (getdport (0), getauth (),
	     MACH_PORT_NULL, MACH_MSG_TYPE_COPY_SEND);

  if ((fcntl (0, F_GETFL) & O_READ) == 0)
    {
      int ttyd = open ("/dev/tty", O_RDWR|O_IGNORE_CTTY);
      if (ttyd >= 0)
	{
	  fcntl (ttyd, F_SETFD, FD_CLOEXEC);
	  stdin = fdopen (ttyd, "r");
	  stdout = stderr = fdopen (ttyd, "w");
	}
    }

  atexit ((void (*) (void)) &sync);

  while (1)
    {
      ssize_t n;

      sync ();
      printf ("# ");
      fflush (stdout);
      n = getline (&linebuf, &linebufsize, stdin);
      if (n == -1)
	{
	  if (feof (stdin))
	    return 0;
	  error (0, errno, "getline");
	  continue;
	}

      if (linebuf[n - 1] == '\n')
	linebuf[--n] = '\0';

      if (n > 0)
	{
	  char *argv[(n + 1) / 2 + 1];
	  int argc;
	  char *line, *p;

	  line = linebuf;
	  argc = 0;
	  while ((p = strsep (&line, " \t\n\f\v")) != NULL)
	    argv[argc++] = p;
	  argv[argc] = NULL;

	  if (!strcmp (argv[0], "exit"))
	    {
	      reap (0);
	      exit (0);
	    }
	  else if (!strcmp (argv[0], "pause"))
	    pause_startup = 1;
	  else if (!strcmp (argv[0], "nopause"))
	    pause_startup = 0;
	  else if (!strcmp (argv[0], "kill"))
	    {
	      if (argc == 1)
		fprintf (stderr, "Usage: kill PID ...\n");
	      else
		{
		  int pid;
		  task_t task;
		  int i;
		  for (i = 1; i < argc; i++)
		    {
		      pid = atoi (argv[i]);
		      printf ("Killing pid %d\n", pid);
		      if (pid)
			{
			  proc_pid2task (proc, pid, &task);
			  task_terminate (task);
			  mach_port_deallocate (mach_task_self (), task);
			}
		    }
		}
	    }
	  else if (!strcmp (argv[0], "cd"))
	    {
	      if (argc != 2)
		fprintf (stderr, "Usage: cd DIRECTORY\n");
	      else if (chdir (argv[1]))
		error (0, errno, "chdir");
	    }
	  else if (!strcmp (argv[0], "exec"))
	    {
	      if (argc == 1)
		fprintf (stderr, "Usage: exec PROGRAM [ARGS...]\n");
	      else
		{
		  char *program;
		  if (strchr (argv[1], '/') != NULL)
		    program = argv[1];
		  else
		    {
		      size_t len = strlen (argv[1]);
		      const char bin[] = "/bin/";
		      program = alloca (sizeof bin + len);
		      memcpy (program, bin, sizeof bin - 1);
		      memcpy (&program[sizeof bin - 1], argv[1], len + 1);
		    }
		  if (execv (program, &argv[1]) == 0)
		    fprintf (stderr, "execv (%s) returned 0!\n", program);
		  else
		    error (0, errno, "execv");
		}
	    }
	  else if (!strcmp (argv[0], "setenv"))
	    {
	      if (argc != 3)
		fprintf (stderr, "Usage: setenv VAR VALUE\n");
	      else if (setenv (argv[1], argv[2], 1))
		error (0, errno, "setenv");
	    }
	  else if (!strcmp (argv[0], "fork"))
	    {
	      pid_t pid = fork ();
	      switch (pid)
		{
		case -1:
		  error (0, errno, "fork");
		  break;
		case 0:
		  printf ("I am the child, PID %d.\n", (int) getpid ());
		  break;
		default:
		  printf ("I am the parent of child with PID %d.\n", pid);
		  reap (argc == 2 && !strcmp (argv[1], "&") ? 0 : pid);
		  break;
		}
	    }
	  else
	    command (argc, argv);
	}
      reap (0);
      fflush (stderr);
    }
}
