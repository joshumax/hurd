/* Start and maintain hurd core servers and system run state

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

/* Written by Michael I. Bushnell and Roland McGrath.  */

/* This is probably more include files than I've ever seen before for
   one file. */
#include <hurd.h>
#include <hurd/fs.h>
#include <hurd/fsys.h>
#include <device/device.h>
#include <stdio.h>
#include <assert.h>
#include <hurd/paths.h>
#include <sys/reboot.h>
#include <sys/file.h>
#include <unistd.h>
#include <string.h>
#include <mach/notify.h>
#include <stdlib.h>
#include <hurd/msg.h>
#include <hurd/term.h>
#include <hurd/fshelp.h>
#include <paths.h>
#include <sys/wait.h>
#include <hurd/msg_server.h>
#include <wire.h>
#include <paths.h>
#include <sys/wait.h>
#include <error.h>
#include <hurd/msg_reply.h>
#include <argz.h>
#include <maptime.h>
#include <version.h>
#include <argp.h>

#include "startup_notify_U.h"
#include "startup_reply_U.h"
#include "startup_S.h"
#include "notify_S.h"
#include "mung_msg_S.h"

/* host_reboot flags for when we crash.  */
int crash_flags = RB_AUTOBOOT;

#define BOOT(flags)	((flags & RB_HALT) ? "halt" : "reboot")

#define _PATH_RUNCOM "/libexec/rc"


/* Multiboot command line used to start the kernel,
   a single string of space-separated words.  */
char *kernel_command_line;

const char *argp_program_version = STANDARD_HURD_VERSION (init);

static struct argp_option
options[] =
{
  {"single-user", 's', 0, 0, "Startup system in single-user mode"},
  {"query",       'q', 0, 0, "Ask for the names of servers to start"},
  {"init-name",   'n', 0, 0 },
  {"crash-debug",  'H', 0, 0, "On system crash, go to kernel debugger"},
  {"debug",       'd', 0, 0 },
  {"fake-boot",   'f', 0, 0, "This hurd hasn't been booted on the raw machine"},
  {"kernel-command-line",
   		  'K', "COMMAND-LINE", 0, "Multiboot command line string"},
  {0,             'x', 0, OPTION_HIDDEN},
  {0}
};

char doc[] = "Start and maintain hurd core servers and system run state";

int booted;			/* Set when the core servers are up.  */

/* This structure keeps track of each notified task.  */
struct ntfy_task
  {
    mach_port_t notify_port;
    struct ntfy_task *next;
    char *name;
  };

/* This structure keeps track of each registered essential task.  */
struct ess_task
  {
    struct ess_task *next;
    task_t task_port;
    char *name;
  };

/* These are linked lists of all of the registered items.  */
struct ess_task *ess_tasks;
struct ntfy_task *ntfy_tasks;

/* Mapped time */
volatile struct mapped_time_value *mapped_time;


/* Our receive right */
mach_port_t startup;

/* Ports to the kernel */
mach_port_t host_priv, device_master;

/* Args to bootstrap, expressed as flags */
int bootstrap_args = 0;

/* Set if something determines we should no longer pass the `autoboot'
   flag to _PATH_RUNCOM. */
int do_fastboot;

/* Stored information for returning proc and auth startup messages. */
mach_port_t procreply, authreply;
mach_msg_type_name_t procreplytype, authreplytype;

/* Our ports to auth and proc. */
mach_port_t authserver;
mach_port_t procserver;

/* Our bootstrap port, on which we call fsys_getpriv and fsys_init. */
mach_port_t bootport;

/* Set iff we are a `fake' bootstrap. */
int fakeboot;

/* The tasks of auth and proc and the bootstrap filesystem. */
task_t authtask, proctask, fstask;

mach_port_t default_ports[INIT_PORT_MAX];
mach_port_t default_dtable[3];
int default_ints[INIT_INT_MAX];

static char **global_argv;
static char *startup_envz;
static size_t startup_envz_len;

void launch_system (void);
void process_signal (int signo);

/** Utility functions **/

/* Read a string from stdin into BUF.  */
static int
getstring (char *buf, size_t bufsize)
{
  if (fgets (buf, bufsize, stdin) != NULL && buf[0] != '\0')
    {
      size_t len = strlen (buf);
      if (buf[len - 1] == '\n' || buf[len - 1] == '\r')
	buf[len - 1] = '\0';
      return 1;
    }
  return 0;
}


/** System shutdown **/

/* Reboot the microkernel.  */
void
reboot_mach (int flags)
{
  if (fakeboot)
    {
      printf ("%s: Would %s Mach with flags %#x\n",
	      program_invocation_short_name, BOOT (flags), flags);
      fflush (stdout);
      exit (1);
    }
  else
    {
      printf ("%s: %sing Mach (flags %#x)...\n",
	      program_invocation_short_name, BOOT (flags), flags);
      fflush (stdout);
      sleep (5);
      while ((errno = host_reboot (host_priv, flags)))
	error (0, errno, "reboot");
      for (;;);
    }
}

/* Reboot the microkernel, specifying that this is a crash. */
void
crash_mach (void)
{
  reboot_mach (crash_flags);
}

/* Notify all tasks that have requested shutdown notifications */
void
notify_shutdown (char *msg)
{
  struct ntfy_task *n;

  for (n = ntfy_tasks; n != NULL; n = n->next)
    {
      error_t err;
      printf ("%s: notifying %s of %s...",
	      program_invocation_short_name, n->name, msg);
      fflush (stdout);
      err = startup_dosync (n->notify_port, 60000); /* 1 minute to reply */
      if (err == MACH_SEND_INVALID_DEST)
	puts ("(no longer present)");
      else if (err)
	puts (strerror (err));
      else
	puts ("done");
      fflush (stdout);
    }
}

/* Reboot the Hurd. */
void
reboot_system (int flags)
{
  notify_shutdown ("shutdown");

  if (fakeboot)
    {
      pid_t *pp;
      u_int npids = 0;
      error_t err;
      int ind;

      err = proc_getallpids (procserver, &pp, &npids);
      if (err == MACH_SEND_INVALID_DEST)
	{
	procbad:
	  /* The procserver must have died.  Give up. */
	  error (0, 0, "Can't simulate crash; proc has died");
	  reboot_mach (flags);
	}
      for (ind = 0; ind < npids; ind++)
	{
	  task_t task;

	  err = proc_pid2task (procserver, pp[ind], &task);
	  if (err == MACH_SEND_INVALID_DEST)
	    goto procbad;
	  else  if (err)
	    {
	      error (0, err, "Getting task for pid %d", pp[ind]);
	      continue;
	    }

	  /* Postpone self so we can finish; postpone proc
	     so that we can finish. */
	  if (task != mach_task_self () && task != proctask)
	    {
	      struct procinfo *pi = 0;
	      u_int pisize = 0;
	      char *noise;
	      unsigned noise_len;
	      err = proc_getprocinfo (procserver, pp[ind], 0,
				      (int **)&pi, &pisize, &noise,&noise_len);
	      if (err == MACH_SEND_INVALID_DEST)
		goto procbad;
	      if (err)
		{
		  error (0, err, "Getting procinfo for pid %d", pp[ind]);
		  continue;
		}
	      if (!(pi->state & PI_NOPARENT))
		{
		  printf ("%s: Killing pid %d\n",
			  program_invocation_short_name, pp[ind]);
		  fflush (stdout);
		  task_terminate (task);
		}
	      if (noise_len > 0)
		vm_deallocate (mach_task_self (),
			       (vm_address_t)noise, noise_len);
	    }
	}
      printf ("%s: Killing proc server\n", program_invocation_short_name);
      fflush (stdout);
      task_terminate (proctask);
      printf ("%s: Exiting", program_invocation_short_name);
      fflush (stdout);
    }
  reboot_mach (flags);
}

/* Reboot the Hurd, specifying that this is a crash. */
void
crash_system (void)
{
  reboot_system (crash_flags);
}



/* Request a dead-name notification sent to our port.  */
static void
request_dead_name (mach_port_t name)
{
  mach_port_t prev;
  mach_port_request_notification (mach_task_self (), name,
				  MACH_NOTIFY_DEAD_NAME, 1, startup,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &prev);
  if (prev != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), prev);
}

/* Record an essential task in the list.  */
static error_t
record_essential_task (const char *name, task_t task)
{
  struct ess_task *et;
  /* Record this task as essential.  */
  et = malloc (sizeof (struct ess_task));
  if (et == NULL)
    return ENOMEM;
  et->task_port = task;
  et->name = strdup (name);
  if (et->name == NULL)
    {
      free (et);
      return ENOMEM;
    }
  et->next = ess_tasks;
  ess_tasks = et;

  /* Dead-name notification on the task port will tell us when it dies.  */
  request_dead_name (task);

#if 0
  /* Taking over the exception port will give us a better chance
     if the task tries to get wedged on a fault.  */
  task_set_special_port (task, TASK_EXCEPTION_PORT, startup);
#endif

  return 0;
}


/** Starting programs **/

/* Run SERVER, giving it INIT_PORT_MAX initial ports from PORTS.
   Set TASK to be the task port of the new image. */
void
run (const char *server, mach_port_t *ports, task_t *task)
{
  char buf[BUFSIZ];
  const char *prog = server;

  if (bootstrap_args & RB_INITNAME)
    {
      printf ("Server file name (default %s): ", server);
      if (getstring (buf, sizeof (buf)))
	prog = buf;
    }

  while (1)
    {
      file_t file;

      file = file_name_lookup (prog, O_EXEC, 0);
      if (file == MACH_PORT_NULL)
	error (0, errno, "%s", prog);
      else
	{
	  task_create (mach_task_self (), 0, task);
	  if (bootstrap_args & RB_KDB)
	    {
	      printf ("Pausing for %s\n", prog);
	      getchar ();
	    }
	  errno = file_exec (file, *task, 0,
			     (char *)prog, strlen (prog) + 1, /* Args.  */
			     startup_envz, startup_envz_len,
			     default_dtable, MACH_MSG_TYPE_COPY_SEND, 3,
			     ports, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
			     default_ints, INIT_INT_MAX,
			     NULL, 0, NULL, 0);
	  if (!errno)
	    break;

	  error (0, errno, "%s", prog);
	}

      printf ("File name for server %s (or nothing to reboot): ", server);
      if (getstring (buf, sizeof (buf)))
	prog = buf;
      else
	crash_system ();
    }

#if 0
  printf ("started %s\n", prog);
  fflush (stdout);
#endif

  /* Dead-name notification on the task port will tell us when it dies,
     so we can crash if we don't make it to a fully bootstrapped Hurd.  */
  request_dead_name (*task);
}

/* Run FILENAME as root with ARGS as its argv (length ARGLEN).  Return
   the task that we started.  If CTTY is set, then make that the
   controlling terminal of the new process and put it in its own login
   collection.  If SETSID is set, put it in a new session.  Return
   0 if the task was not created successfully. */
pid_t
run_for_real (char *filename, char *args, int arglen, mach_port_t ctty,
	      int setsid)
{
  file_t file;
  error_t err;
  task_t task;
  char *progname;
  int pid;

#if 0
  char buf[512];
  do
    {
      printf ("File name [%s]: ", filename);
      if (getstring (buf, sizeof (buf)) && *buf)
	filename = buf;
      file = file_name_lookup (filename, O_EXEC, 0);
      if (!file)
	error (0, errno, "%s", filename);
    }
  while (!file);
#else
  file = file_name_lookup (filename, O_EXEC, 0);
  if (!file)
    {
      error (0, errno, "%s", filename);
      return 0;
    }
#endif

  task_create (mach_task_self (), 0, &task);
  proc_child (procserver, task);
  proc_task2pid (procserver, task, &pid);
  proc_task2proc (procserver, task, &default_ports[INIT_PORT_PROC]);
  proc_mark_exec (default_ports[INIT_PORT_PROC]);
  if (setsid)
    proc_setsid (default_ports[INIT_PORT_PROC]);
  if (ctty != MACH_PORT_NULL)
    {
      term_getctty (ctty, &default_ports[INIT_PORT_CTTYID]);
      io_mod_owner (ctty, -pid);
      proc_make_login_coll (default_ports[INIT_PORT_PROC]);
    }
  if (bootstrap_args & RB_KDB)
    {
      printf ("Pausing for %s\n", filename);
      getchar ();
    }
  progname = strrchr (filename, '/');
  if (progname)
    ++progname;
  else
    progname = filename;
  err = file_exec (file, task, 0,
		   args, arglen,
		   startup_envz, startup_envz_len,
		   default_dtable, MACH_MSG_TYPE_COPY_SEND, 3,
		   default_ports, MACH_MSG_TYPE_COPY_SEND,
		   INIT_PORT_MAX,
		   default_ints, INIT_INT_MAX,
		   NULL, 0, NULL, 0);
  mach_port_deallocate (mach_task_self (), default_ports[INIT_PORT_PROC]);
  mach_port_deallocate (mach_task_self (), task);
  if (ctty != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (),
			    default_ports[INIT_PORT_CTTYID]);
      default_ports[INIT_PORT_CTTYID] = MACH_PORT_NULL;
    }
  mach_port_deallocate (mach_task_self (), file);
  if (err)
    {
      error (0, err, "Cannot execute %s", filename);
      return 0;
    }
  return pid;
}


/** Main program and setup **/

static int
demuxer (mach_msg_header_t *inp,
	 mach_msg_header_t *outp)
{
  extern int notify_server (), startup_server (), msg_server ();

  return (notify_server (inp, outp) ||
	  msg_server (inp, outp) ||
	  startup_server (inp, outp));
}

static int
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'q': bootstrap_args |= RB_ASKNAME; break;
    case 's': bootstrap_args |= RB_SINGLE; break;
    case 'd': bootstrap_args |= RB_KDB; break;
    case 'n': bootstrap_args |= RB_INITNAME; break;
    case 'f': fakeboot = 1; break;
    case 'H': crash_flags = RB_DEBUGGER; break;
    case 'x': /* NOP */ break;
    default: return ARGP_ERR_UNKNOWN;

    case 'K':
      kernel_command_line = arg;
      /* XXX When this is really in use,
	 this should do some magical parsing for options.  */
      break;
    }
  return 0;
}

int
main (int argc, char **argv, char **envp)
{
  volatile int err;
  int i;
  mach_port_t consdev;
  struct argp argp = { options, parse_opt, 0, doc };

  /* Parse the arguments */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (getpid () > 0)
    error (2, 0, "can only be run by bootstrap filesystem");

  global_argv = argv;

  /* Fetch a port to the bootstrap filesystem, the host priv and
     master device ports, and the console.  */
  if (task_get_bootstrap_port (mach_task_self (), &bootport)
      || fsys_getpriv (bootport, &host_priv, &device_master, &fstask)
      || device_open (device_master, D_WRITE, "console", &consdev))
    crash_mach ();

  wire_task_self ();

  /* Clear our bootstrap port so our children don't inherit it.  */
  task_set_bootstrap_port (mach_task_self (), MACH_PORT_NULL);

  stderr = stdout = mach_open_devstream (consdev, "w");
  stdin = mach_open_devstream (consdev, "r");
  if (stdout == NULL || stdin == NULL)
    crash_mach ();
  setbuf (stdout, NULL);

  err = argz_create (envp, &startup_envz, &startup_envz_len);
  assert_perror (err);

  /* At this point we can use assert to check for errors.  */
  err = mach_port_allocate (mach_task_self (),
			    MACH_PORT_RIGHT_RECEIVE, &startup);
  assert_perror (err);
  err = mach_port_insert_right (mach_task_self (), startup, startup,
				MACH_MSG_TYPE_MAKE_SEND);
  assert_perror (err);

  /* Crash if the boot filesystem task dies.  */
  request_dead_name (fstask);

  /* Set up the set of ports we will pass to the programs we exec.  */
  for (i = 0; i < INIT_PORT_MAX; i++)
    switch (i)
      {
      case INIT_PORT_CRDIR:
	default_ports[i] = getcrdir ();
	break;
      case INIT_PORT_CWDIR:
	default_ports[i] = getcwdir ();
	break;
      default:
	default_ports[i] = MACH_PORT_NULL;
	break;
      }

  default_dtable[0] = getdport (0);
  default_dtable[1] = getdport (1);
  default_dtable[2] = getdport (2);

  /* All programs we start should ignore job control stop signals.
     That way Posix.1 B.2.2.2 is satisfied where it says that programs
     not run under job control shells are protected.  */
  default_ints[INIT_SIGIGN] = (sigmask (SIGTSTP)
			       | sigmask (SIGTTIN)
			       | sigmask (SIGTTOU));

  default_ports[INIT_PORT_BOOTSTRAP] = startup;
  run ("/hurd/proc", default_ports, &proctask);
  printf (" proc");
  fflush (stdout);
  run ("/hurd/auth", default_ports, &authtask);
  printf (" auth");
  fflush (stdout);
  default_ports[INIT_PORT_BOOTSTRAP] = MACH_PORT_NULL;

  /* Wait for messages.  When both auth and proc have started, we
     run launch_system which does the rest of the boot.  */
  while (1)
    {
      err = mach_msg_server (demuxer, 0, startup);
      assert_perror (err);
    }
}

void
launch_core_servers (void)
{
  mach_port_t old;
  mach_port_t authproc, fsproc, procproc;
  error_t err;

  /* Reply to the proc and auth servers.   */
  startup_procinit_reply (procreply, procreplytype, 0,
			  mach_task_self (), authserver,
			  host_priv, MACH_MSG_TYPE_COPY_SEND,
			  device_master, MACH_MSG_TYPE_COPY_SEND);
  if (!fakeboot)
    {
      mach_port_deallocate (mach_task_self (), device_master);
      device_master = 0;
    }

  proc_mark_exec (procserver);

  /* Declare that the filesystem and auth are our children. */
  proc_child (procserver, fstask);
  proc_child (procserver, authtask);

  proc_task2proc (procserver, authtask, &authproc);
  proc_mark_exec (authproc);
  startup_authinit_reply (authreply, authreplytype, 0, authproc,
			  MACH_MSG_TYPE_MOVE_SEND);

  /* Give the library our auth and proc server ports.  */
  _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], authserver);
  _hurd_port_set (&_hurd_ports[INIT_PORT_PROC], procserver);

  /* Do NOT run _hurd_proc_init!  That will start signals, which we do not
     want.  We listen to our own message port.  Tell the proc server where
     our args and environment are.  */
  proc_set_arg_locations (procserver,
			  (vm_address_t) global_argv, (vm_address_t) environ);

  default_ports[INIT_PORT_AUTH] = authserver;

  /* Declare that the proc server is our child.  */
  proc_child (procserver, proctask);
  err = proc_task2proc (procserver, proctask, &procproc);
  if (!err)
    {
      proc_mark_exec (procproc);
      mach_port_deallocate (mach_task_self (), procproc);
    }

  proc_register_version (procserver, host_priv, "init", "", HURD_VERSION);

  /* Get the bootstrap filesystem's proc server port.
     We must do this before calling proc_setmsgport below.  */
  proc_task2proc (procserver, fstask, &fsproc);
  proc_mark_exec (fsproc);

#if 0
  printf ("Init has completed.\n");
  fflush (stdout);
#endif
  printf (".\n");
  fflush (stdout);

  /* Tell the proc server our msgport.  Be sure to do this after we are all
     done making requests of proc.  Once we have done this RPC, proc
     assumes it can send us requests, so we cannot block on proc again
     before accepting more RPC requests!  However, we must do this before
     calling fsys_init, because fsys_init blocks on exec_init, and
     exec_init will block waiting on our message port.  */
  proc_setmsgport (procserver, startup, &old);
  if (old != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), old);

  /* Give the bootstrap FS its proc and auth ports.  */
  errno = fsys_init (bootport, fsproc, MACH_MSG_TYPE_MOVE_SEND, authserver);
  if (errno)
    error (0, errno, "fsys_init"); /* Not necessarily fatal.  */
}

/* Set up the initial value of the standard exec data. */
void
init_stdarrays ()
{
  auth_t nullauth;
  mach_port_t pt;
  mach_port_t ref;
  mach_port_t *std_port_array;
  int *std_int_array;
  int i;

  std_port_array = alloca (sizeof (mach_port_t) * INIT_PORT_MAX);
  std_int_array = alloca (sizeof (int) * INIT_INT_MAX);

  bzero (std_port_array, sizeof (mach_port_t) * INIT_PORT_MAX);
  bzero (std_int_array, sizeof (int) * INIT_INT_MAX);

  __USEPORT (AUTH, auth_makeauth (port, 0, MACH_MSG_TYPE_COPY_SEND, 0,
				  0, 0, 0, 0, 0, 0, 0, 0, &nullauth));

  pt = getcwdir ();
  ref = mach_reply_port ();
  io_reauthenticate (pt, ref, MACH_MSG_TYPE_MAKE_SEND);
  auth_user_authenticate (nullauth, ref, MACH_MSG_TYPE_MAKE_SEND,
			  &std_port_array[INIT_PORT_CWDIR]);
  mach_port_destroy (mach_task_self (), ref);
  mach_port_deallocate (mach_task_self (), pt);

  pt = getcrdir ();
  ref = mach_reply_port ();
  io_reauthenticate (pt, ref, MACH_MSG_TYPE_MAKE_SEND);
  auth_user_authenticate (nullauth, ref, MACH_MSG_TYPE_MAKE_SEND,
			  &std_port_array[INIT_PORT_CRDIR]);
  mach_port_destroy (mach_task_self (), ref);
  mach_port_deallocate (mach_task_self (), pt);

  std_port_array[INIT_PORT_AUTH] = nullauth;

  std_int_array[INIT_UMASK] = CMASK;

  __USEPORT (PROC, proc_setexecdata (port, std_port_array,
				     MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
				     std_int_array, INIT_INT_MAX));
  for (i = 0; i < INIT_PORT_MAX; i++)
    mach_port_deallocate (mach_task_self (), std_port_array[i]);
}

/* Open /dev/console.  If it isn't there, or it isn't a terminal, then
   create /tmp/console and put the terminal on it.  If we get EROFS,
   in trying to create /tmp/console then as a last resort, put the
   console on /tmp itself.

   In any case, after the console has been opened, set it appropriately
   in default_dtable.  Also return a port right for the terminal. */
file_t
open_console ()
{
#define TERMINAL_FIRST_TRY "/hurd/term\0/tmp/console\0device\0console"
#define TERMINAL_SECOND_TRY "/hurd/term\0/tmp\0device\0console"
  mach_port_t term;
  static char *termname;
  struct stat st;
  error_t err = 0;

  if (booted)
    {
      term = file_name_lookup (termname, O_RDWR, 0);
      return term;
    }

  termname = _PATH_CONSOLE;
  term = file_name_lookup (termname, O_RDWR, 0);
  if (term != MACH_PORT_NULL)
    err = io_stat (term, &st);
  else
    err = errno;
  if (err)
    error (0, err, "%s", termname);
  else if (st.st_fstype != FSTYPE_TERM)
    error (0, 0, "%s: Not a terminal", termname);

  if (term == MACH_PORT_NULL || err || st.st_fstype != FSTYPE_TERM)
    /* Start the terminal server ourselves. */
    {
      size_t argz_len;		/* Length of args passed to translator.  */
      char *terminal;		/* Name of term translator.  */
      mach_port_t control;	/* Control port for term translator.  */
      int try = 1;

      error_t open_node (int flags,
			 mach_port_t *underlying,
			 mach_msg_type_name_t *underlying_type)
	{
	  term = file_name_lookup (termname, flags | O_CREAT|O_NOTRANS, 0666);
	  if (term == MACH_PORT_NULL)
	    {
	      error (0, errno, "%s", termname);
	      return errno;
	    }

	  *underlying = term;
	  *underlying_type = MACH_MSG_TYPE_COPY_SEND;

	  return 0;
	}

    retry:
      bootstrap_args |= RB_SINGLE;

      if (try == 1)
	{
	  terminal = TERMINAL_FIRST_TRY;
	  argz_len = sizeof TERMINAL_FIRST_TRY;
	  try = 2;
	}
      else if (try == 2)
	{
	  terminal = TERMINAL_SECOND_TRY;
	  argz_len = sizeof TERMINAL_SECOND_TRY;
	  try = 3;
	}
      else
	goto fail;

      termname = terminal + strlen (terminal) + 1; /* first arg is name */

      /* The callback to start_translator opens TERM as a side effect.  */
      errno =
	fshelp_start_translator (open_node, terminal, terminal, argz_len, 3000,
				 &control);
      if (errno)
	{
	  error (0, errno, "%s", terminal);
	  goto retry;
	}

      errno = file_set_translator (term, 0, FS_TRANS_SET, 0, 0, 0,
				   control, MACH_MSG_TYPE_COPY_SEND);
      mach_port_deallocate (mach_task_self (), control);
      if (errno)
	{
	  error (0, errno, "%s", termname);
	  goto retry;
	}
      mach_port_deallocate (mach_task_self (), term);

      /* Now repeat the open. */
      term = file_name_lookup (termname, O_RDWR, 0);
      if (term == MACH_PORT_NULL)
	{
	  error (0, errno, "%s", termname);
	  goto retry;
	}
      errno = io_stat (term, &st);
      if (errno)
	{
	  error (0, errno, "%s", termname);
	  term = MACH_PORT_NULL;
	  goto retry;
	}
      if (st.st_fstype != FSTYPE_TERM)
	{
	  error (0, 0, "%s: Not a terminal", termname);
	  term = MACH_PORT_NULL;
	  goto retry;
	}

      if (term)
	error (0, 0, "Using temporary console %s", termname);
    }

 fail:

  /* At this point either TERM is the console or it's null.  If it's
     null, then don't do anything, and our fd's will be copied.
     Otherwise, open fd's 0, 1, and 2. */
  if (term != MACH_PORT_NULL)
    {
      int fd = openport (term, O_RDWR);
      if (fd < 0)
	assert_perror (errno);

      dup2 (fd, 0);
      close (fd);
      dup2 (0, 1);
      dup2 (0, 2);

      fclose (stdin);
      stdin = fdopen (0, "r");

      /* Don't reopen our output channel for reliability's sake. */

      /* Set ports in init_dtable for programs we start. */
      mach_port_deallocate (mach_task_self (), default_dtable[0]);
      mach_port_deallocate (mach_task_self (), default_dtable[1]);
      mach_port_deallocate (mach_task_self (), default_dtable[2]);
      default_dtable[0] = getdport (0);
      default_dtable[1] = getdport (1);
      default_dtable[2] = getdport (2);
    }
  else
    error (0, 0, "Cannot open console");

  return term;
}


/* Frobnicate the kernel task and the proc server's idea of it (PID 2),
   so the kernel command line can be read as for a normal Hurd process.  */

void
frob_kernel_process (void)
{
  error_t err;
  int argc, i;
  char *argz, *entry;
  size_t argzlen;
  size_t windowsz;
  vm_address_t mine, his;
  task_t task;
  process_t proc, kbs;

  err = proc_pid2task (procserver, 2, &task);
  if (err)
    {
      error (0, err, "cannot get kernel task port");
      return;
    }
  err = proc_task2proc (procserver, task, &proc);
  if (err)
    {
      error (0, err, "cannot get kernel task's proc server port");
      mach_port_deallocate (mach_task_self (), task);
      return;
    }

  /* Mark the kernel task as an essential task so that we never
     want to task_terminate it.  */
  err = record_essential_task ("kernel", task);
  assert_perror (err);

  err = task_get_bootstrap_port (task, &kbs);
  assert_perror (err);
  if (kbs == MACH_PORT_NULL)
    {
      /* The kernel task has no bootstrap port set, so we are presumably
	 the first Hurd to boot.  Install the kernel task's proc port from
	 this Hurd's proc server as the task bootstrap port.  Additional
	 Hurds will see this.  */

      err = task_set_bootstrap_port (task, proc);
      if (err)
	error (0, err, "cannot set kernel task's bootstrap port");

      if (fakeboot)
	error (0, 0, "warning: --fake-boot specified but I see no other Hurd");
    }
  else
    {
      /* The kernel task has a bootstrap port set.  Perhaps it is its proc
	 server port from another Hurd.  If so, propagate the kernel
	 argument locations from that Hurd rather than diddling with the
	 kernel task ourselves.  */

      vm_address_t kargv, kenvp;
      err = proc_get_arg_locations (kbs, &kargv, &kenvp);
      mach_port_deallocate (mach_task_self (), kbs);
      if (err)
	error (0, err, "kernel task bootstrap port (ignoring)");
      else
	{
	  err = proc_set_arg_locations (proc, kargv, kenvp);
	  if (err)
	    error (0, err, "cannot propagate original kernel command line");
	  else
	    {
	      mach_port_deallocate (mach_task_self (), proc);
	      mach_port_deallocate (mach_task_self (), task);
	      if (! fakeboot)
		error (0, 0, "warning: "
		       "I see another Hurd, but --fake-boot was not given");
	      return;
	    }
	}
    }

  if (!kernel_command_line)
    kernel_command_line = getenv ("MULTIBOOT_CMDLINE") ?: "(kernel)";


  /* The variable `kernel_command_line' contains the multiboot command line
     used to boot the kernel, a single string of space-separated words.

     We will slice that up into words, and then write into the kernel task
     a page containing a canonical argv array and argz of those words.  */

  err = argz_create_sep (kernel_command_line, ' ', &argz, &argzlen);
  assert_perror (err);
  argc = argz_count (argz, argzlen);

  windowsz = round_page (((argc + 1) * sizeof (char *)) + argzlen);

  err = vm_allocate (mach_task_self (), &mine, windowsz, 1);
  assert_perror (err);
  err = vm_allocate (mach_task_self (), &his, windowsz, 1);
  if (err)
    {
      error (0, err, "cannot allocate %Zu bytes in kernel task", windowsz);
      free (argz);
      mach_port_deallocate (mach_task_self (), proc);
      mach_port_deallocate (mach_task_self (), task);
      return;
    }

  for (i = 0, entry = argz; entry != NULL;
       ++i, entry = argz_next (argz, argzlen, entry))
    ((char **) mine)[i] = ((char *) &((char **) his)[argc + 1]
			   + (entry - argz));
  ((char **) mine)[argc] = NULL;
  memcpy (&((char **) mine)[argc + 1], argz, argzlen);

  free (argz);

  /* We have the data all set up in our copy, now just write it over.  */
  err = vm_write (task, his, mine, windowsz);
  mach_port_deallocate (mach_task_self (), task);
  vm_deallocate (mach_task_self (), mine, windowsz);
  if (err)
    {
      error (0, err, "cannot write command line into kernel task");
      return;
    }

  /* The argument vector is set up in the kernel task at address HIS.
     Finally, we can inform the proc server where to find it.  */
  err = proc_set_arg_locations (proc, his, his + (argc * sizeof (char *)));
  mach_port_deallocate (mach_task_self (), proc);
  if (err)
    error (0, err, "proc_set_arg_locations for kernel task");
}

/** Single and multi user transitions **/

/* Current state of the system. */
enum
{
  INITIAL,
  SINGLE,
  RUNCOM,
  MULTI,
} system_state;

pid_t shell_pid;		/* PID of single-user shell.  */
pid_t rc_pid;			/* PID of rc script */


#include "ttys.h"


/* Start the single-user environment.  This can only be done
   when the core servers have fully started.  We know that
   startup_essential_task is the last thing they do before being
   ready to handle requests, so we start this once all the necessary
   servers have identified themselves that way. */
void
launch_single_user ()
{
  char shell[1024];
  mach_port_t term;

  printf ("Single-user environment:");
  fflush (stdout);

  term = open_console ();

  system_state = SINGLE;

#if 0
  printf ("Shell program [%s]: ", _PATH_BSHELL);
  if (! getstring (shell, sizeof shell))
#endif
    strcpy (shell, _PATH_BSHELL);

  /* The shell needs a real controlling terminal, so set that up here. */
  shell_pid = run_for_real (shell, shell, strlen (shell) + 1, term, 1);
  mach_port_deallocate (mach_task_self (), term);
  if (shell_pid == 0)
    crash_system ();
  printf (" shell.\n");
  fflush (stdout);
}

/* Run /etc/rc as a shell script.  Return non-zero if we fail.  */
int
process_rc_script ()
{
  char *rcargs;
  size_t rcargslen;
  mach_port_t term;

  if (do_fastboot)
    {
      rcargs = malloc (rcargslen = sizeof _PATH_RUNCOM);
      strcpy (rcargs, _PATH_RUNCOM);
    }
  else
    {
      rcargslen = asprintf (&rcargs, "%s%c%s", _PATH_RUNCOM, '\0', "autoboot");
      rcargslen++;		/* final null */
    }

  term = open_console ();

  system_state = RUNCOM;

  rc_pid = run_for_real (rcargs, rcargs, rcargslen, term, 1);
  free (rcargs);
  mach_port_deallocate (mach_task_self (), term);
  return ! rc_pid;
}

/* Start up multi-user state. */
void
launch_multi_user ()
{
  int fail;

  if (!mapped_time)
    maptime_map (1, 0, &mapped_time);

  fail = init_ttys ();
  if (fail)
    launch_single_user ();
  else
    {
      system_state = MULTI;
      fail = startup_ttys ();
      if (fail)
	launch_single_user ();
    }
}

void
launch_system ()
{
  if (bootstrap_args & RB_SINGLE)
    launch_single_user ();
  else
    {
      if (process_rc_script ())
	launch_single_user ();
    }
}


/* Kill all the outstanding processes with SIGNO.  Return 1 if
   there were no tasks left to kill. */
int
kill_everyone (int signo)
{
  pid_t pidbuf[100], *pids = pidbuf;
  mach_msg_type_number_t i, npids = 100;
  task_t tk;
  struct ess_task *es;
  mach_port_t msg;
  int didany;
  int nloops;
  error_t err;

  for (nloops = 10; nloops; nloops--)
    {
      if (nloops < 9)
	/* Give it a rest for folks to have a chance to die */
	sleep (1);

      didany = 0;
      err = proc_getallpids (procserver, &pids, &npids);
      if (!err)
	{
	  for (i = 0; i < npids; i++)
	    {
	      if (pids[i] == 1 /* us */
		  || pids[i] == 3 /* default pager for now XXX */)
		continue;

	      /* See if the task is essential */
	      err = proc_pid2task (procserver, pids[i], &tk);
	      if (err)
		continue;

	      for (es = ess_tasks; es; es = es->next)
		if (tk == es->task_port)
		  {
		    /* Skip this one */
		    mach_port_deallocate (mach_task_self (), tk);
		    break;
		  }
	      if (es)
		continue;

	      /* Kill it */
	      if (signo == SIGKILL)
		{
		  task_terminate (tk);
		  didany = 1;
		}
	      else
		{
		  err = proc_getmsgport (procserver, pids[i], &msg);
		  if (err)
		    {
		      mach_port_deallocate (mach_task_self (), tk);
		      continue;
		    }

		  didany = 1;
		  msg_sig_post (msg, signo, 0, tk);
		  mach_port_deallocate (mach_task_self (), tk);
		}
	    }
	}
      if (pids != pidbuf)
	vm_deallocate (mach_task_self (), (vm_address_t) pids,
		       npids * sizeof (pid_t));
      if (!didany)
	return 1;
    }
  return 0;
}

/* Kill outstanding multi-user sessions */
void
kill_multi_user ()
{
  int sigs[3] = {SIGHUP, SIGTERM, SIGKILL};
  int stage;

  free_ttys ();

  for (stage = 0; stage < 3; stage++)
    if (kill_everyone (sigs[stage]))
      break;

  /* Notify tasks that they are about to die. */
  notify_shutdown ("transition to single-user");

  if (stage == 3)
    error (0, 0, "warning: some processes wouldn't die; `ps -AlM' advised");
}

/* SIGNO has arrived and has been validated.  Do whatever work it
   implies. */
void
process_signal (int signo)
{
  int fail;

  switch (signo)
    {
    case SIGTERM:
      if (system_state == MULTI)
	{
	  /* Drop back to single user. */
	  kill_multi_user ();
	  launch_single_user ();
	}
      break;

    case SIGHUP:
      if (system_state == MULTI)
	reread_ttys ();
      break;

    case SIGCHLD:
      {
	/* A child died.  Find its status.  */
	int status;
	pid_t pid;

	for (;;)
	  {
	    pid = waitpid (WAIT_ANY, &status, WNOHANG | WUNTRACED);
	    if (pid <= 0)
	      return;

	    if (pid == shell_pid && system_state == SINGLE)
	      {
		if (WIFSIGNALED (status))
		  {
		    error (0, 0,
			   "Single-user terminated abnormally (%s), restarting",
			   strsignal (WTERMSIG (status)));
		    launch_single_user ();
		  }
		else if (WIFSTOPPED (status))
		  {
		    error (0, 0,
			   "Single-user stopped (%s), killing and restarting",
			   strsignal (WSTOPSIG (status)));
		    kill (shell_pid, SIGKILL);
		    launch_single_user ();
		  }
		else
		  {
		    do_fastboot = 1;
		    fail = process_rc_script ();
		    if (fail)
		      {
			do_fastboot = 0;
			launch_single_user ();
		      }
		  }
	      }
	    else if (pid == rc_pid && system_state == RUNCOM)
	      {
		if (WIFSIGNALED (status))
		  {
		    error (0, 0,
			   "%s terminated abnormally (%s), \
going to single user mode",
			   _PATH_RUNCOM, strsignal (WTERMSIG (status)));
		    launch_single_user ();
		  }
		else if (WIFSTOPPED (status))
		  {
		    error (0, 0,
			   "%s stopped (%s), \
killing it and going to single user mode",
			   _PATH_RUNCOM, strsignal (WSTOPSIG (status)));
		    kill (rc_pid, SIGKILL);
		    launch_single_user ();
		  }
		else if (WEXITSTATUS (status))
		  launch_single_user ();
		else
		  launch_multi_user ();
	      }
	    else if (system_state == MULTI)
	      restart_terminal (pid);
	  }
	break;
      }
    default:
      break;
    }
}


/** RPC servers **/

kern_return_t
S_startup_procinit (startup_t server,
		    mach_port_t reply,
		    mach_msg_type_name_t reply_porttype,
		    process_t proc,
		    mach_port_t *startuptask,
		    auth_t *auth,
		    mach_port_t *priv,
		    mach_msg_type_name_t *hostprivtype,
		    mach_port_t *dev,
		    mach_msg_type_name_t *devtype)
{
  if (procserver)
    /* Only one proc server.  */
    return EPERM;

  procserver = proc;

  procreply = reply;
  procreplytype = reply_porttype;

  /* Save the reply port until we get startup_authinit.  */
  if (authserver)
    launch_core_servers ();

  return MIG_NO_REPLY;
}

/* Called by the auth server when it starts up.  */

kern_return_t
S_startup_authinit (startup_t server,
		    mach_port_t reply,
		    mach_msg_type_name_t reply_porttype,
		    mach_port_t auth,
		    mach_port_t *proc,
		    mach_msg_type_name_t *proctype)
{
  if (authserver)
    /* Only one auth server.  */
    return EPERM;

  authserver = auth;

  /* Save the reply port until we get startup_procinit.  */
  authreply = reply;
  authreplytype = reply_porttype;

  if (procserver)
    launch_core_servers ();

  return MIG_NO_REPLY;
}


kern_return_t
S_startup_essential_task (mach_port_t server,
			  mach_port_t reply,
			  mach_msg_type_name_t replytype,
			  task_t task,
			  mach_port_t excpt,
			  char *name,
			  mach_port_t credential)
{
  static int authinit, procinit, execinit;
  int fail;

  if (credential != host_priv)
    return EPERM;

  fail = record_essential_task (name, task);
  if (fail)
    return fail;

  mach_port_deallocate (mach_task_self (), credential);

  if (!booted)
    {
      if (!strcmp (name, "auth"))
	authinit = 1;
      else if (!strcmp (name, "exec"))
	execinit = 1;
      else if (!strcmp (name, "proc"))
	procinit = 1;

      if (authinit && execinit && procinit)
	{
	  /* Reply to this RPC, after that everything
	     is ready for real startup to begin. */
	  startup_essential_task_reply (reply, replytype, 0);

	  init_stdarrays ();
	  frob_kernel_process ();

	  launch_system ();

	  booted = 1;

	  return MIG_NO_REPLY;
	}
    }

  return 0;
}

kern_return_t
S_startup_request_notification (mach_port_t server,
				mach_port_t notify,
				char *name)
{
  struct ntfy_task *nt;

  request_dead_name (notify);

  /* Note that the ntfy_tasks list is kept in inverse order of the
     calls; this is important.  We need later notification requests
     to get executed first.  */
  nt = malloc (sizeof (struct ntfy_task));
  nt->notify_port = notify;
  nt->next = ntfy_tasks;
  ntfy_tasks = nt;
  nt->name = malloc (strlen (name) + 1);
  strcpy (nt->name, name);
  return 0;
}

kern_return_t
do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t name)
{
  struct ntfy_task *nt, *pnt;
  struct ess_task *et;

  assert (notify == startup);

  /* Deallocate the extra reference the notification carries. */
  mach_port_deallocate (mach_task_self (), name);

  for (et = ess_tasks; et != NULL; et = et->next)
    if (et->task_port == name)
      /* An essential task has died.  */
      {
	error (0, 0, "Crashing system; essential task %s died", et->name);
	crash_system ();
      }

  for (nt = ntfy_tasks, pnt = NULL; nt != NULL; pnt = nt, nt = nt->next)
    if (nt->notify_port == name)
      {
	/* Someone who wanted to be notified is gone.  */
	mach_port_deallocate (mach_task_self (), name);
	if (pnt != NULL)
	  pnt->next = nt->next;
	else
	  ntfy_tasks = nt->next;
	free (nt);

	return 0;
      }

  if (! booted)
    {
      /* The system has not come up yet, so essential tasks are not yet
	 registered.  But the essential servers involved in the bootstrap
	 handshake might crash before completing it, so we have requested
	 dead-name notification on those tasks.  */
      static const struct { task_t *taskp; const char *name; } boots[] =
        {
	  {&fstask, "bootstrap filesystem"},
	  {&authtask, "auth"},
	  {&proctask, "proc"},
	};
      size_t i;
      for (i = 0; i < sizeof boots / sizeof boots[0]; ++i)
	if (name == *boots[i].taskp)
	  {
	    error (0, 0, "Crashing system; %s server died during bootstrap",
		   boots[i].name);
	    crash_mach ();
	  }
      error (0, 0, "BUG!  Unexpected dead-name notification (name %#x)", name);
      crash_mach ();
    }

  return 0;
}

kern_return_t
S_startup_reboot (mach_port_t server,
		  mach_port_t refpt,
		  int code)
{
  if (refpt != host_priv)
    return EPERM;

  reboot_system (code);
  for (;;);
}

/* Stubs for unused notification RPCs.  */

kern_return_t
do_mach_notify_port_destroyed (mach_port_t notify,
			       mach_port_t rights)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_send_once (mach_port_t notify)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_no_senders (mach_port_t port, mach_port_mscount_t mscount)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_port_deleted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_msg_accepted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

/* msg server */

kern_return_t
S_msg_sig_post_untraced (mach_port_t msgport,
			 mach_port_t reply, mach_msg_type_name_t reply_type,
			 int signo, natural_t sigcode, mach_port_t refport)
{
  if (refport != mach_task_self ())
    return EPERM;
  mach_port_deallocate (mach_task_self (), refport);

  /* Reply immediately */
  msg_sig_post_untraced_reply (reply, reply_type, 0);

  process_signal (signo);
  return MIG_NO_REPLY;
}

kern_return_t
S_msg_sig_post (mach_port_t msgport,
		mach_port_t reply, mach_msg_type_name_t reply_type,
		int signo, natural_t sigcode, mach_port_t refport)
{
  if (refport != mach_task_self ())
    return EPERM;
  mach_port_deallocate (mach_task_self (), refport);

  /* Reply immediately */
  msg_sig_post_reply (reply, reply_type, 0);

  process_signal (signo);
  return MIG_NO_REPLY;
}


/* For the rest of the msg functions, just call the C library's
   internal server stubs usually run in the signal thread.  */

kern_return_t
S_msg_proc_newids (mach_port_t process,
	mach_port_t task,
	pid_t ppid,
	pid_t pgrp,
	int orphaned)
{ return _S_msg_proc_newids (process, task, ppid, pgrp, orphaned); }


kern_return_t
S_msg_add_auth (mach_port_t process,
	auth_t auth)
{ return _S_msg_add_auth (process, auth); }


kern_return_t
S_msg_del_auth (mach_port_t process,
	mach_port_t task,
	intarray_t uids,
	mach_msg_type_number_t uidsCnt,
	intarray_t gids,
	mach_msg_type_number_t gidsCnt)
{ return _S_msg_del_auth (process, task, uids, uidsCnt, gids, gidsCnt); }


kern_return_t
S_msg_get_init_port (mach_port_t process,
	mach_port_t refport,
	int which,
	mach_port_t *port,
	mach_msg_type_name_t *portPoly)
{ return _S_msg_get_init_port (process, refport, which, port, portPoly); }


kern_return_t
S_msg_set_init_port (mach_port_t process,
	mach_port_t refport,
	int which,
	mach_port_t port)
{ return _S_msg_set_init_port (process, refport, which, port); }


kern_return_t
S_msg_get_init_ports (mach_port_t process,
	mach_port_t refport,
	portarray_t *ports,
	mach_msg_type_name_t *portsPoly,
	mach_msg_type_number_t *portsCnt)
{ return _S_msg_get_init_ports (process, refport, ports, portsPoly, portsCnt); }


kern_return_t
S_msg_set_init_ports (mach_port_t process,
	mach_port_t refport,
	portarray_t ports,
	mach_msg_type_number_t portsCnt)
{ return _S_msg_set_init_ports (process, refport, ports, portsCnt); }


kern_return_t
S_msg_get_init_int (mach_port_t process,
	mach_port_t refport,
	int which,
	int *value)
{ return _S_msg_get_init_int (process, refport, which, value); }


kern_return_t
S_msg_set_init_int (mach_port_t process,
	mach_port_t refport,
	int which,
	int value)
{ return _S_msg_set_init_int (process, refport, which, value); }


kern_return_t
S_msg_get_init_ints (mach_port_t process,
	mach_port_t refport,
	intarray_t *values,
	mach_msg_type_number_t *valuesCnt)
{ return _S_msg_get_init_ints (process, refport, values, valuesCnt); }


kern_return_t
S_msg_set_init_ints (mach_port_t process,
	mach_port_t refport,
	intarray_t values,
	mach_msg_type_number_t valuesCnt)
{ return _S_msg_set_init_ints (process, refport, values, valuesCnt); }


kern_return_t
S_msg_get_dtable (mach_port_t process,
	mach_port_t refport,
	portarray_t *dtable,
	mach_msg_type_name_t *dtablePoly,
	mach_msg_type_number_t *dtableCnt)
{ return _S_msg_get_dtable (process, refport, dtable, dtablePoly, dtableCnt); }


kern_return_t
S_msg_set_dtable (mach_port_t process,
	mach_port_t refport,
	portarray_t dtable,
	mach_msg_type_number_t dtableCnt)
{ return _S_msg_set_dtable (process, refport, dtable, dtableCnt); }


kern_return_t
S_msg_get_fd (mach_port_t process,
	mach_port_t refport,
	int fd,
	mach_port_t *port,
	mach_msg_type_name_t *portPoly)
{ return _S_msg_get_fd (process, refport, fd, port, portPoly); }


kern_return_t
S_msg_set_fd (mach_port_t process,
	mach_port_t refport,
	int fd,
	mach_port_t port)
{ return _S_msg_set_fd (process, refport, fd, port); }


kern_return_t
S_msg_get_environment (mach_port_t process,
	data_t *value,
	mach_msg_type_number_t *valueCnt)
{ return _S_msg_get_environment (process, value, valueCnt); }


kern_return_t
S_msg_set_environment (mach_port_t process,
	mach_port_t refport,
	data_t value,
	mach_msg_type_number_t valueCnt)
{ return _S_msg_set_environment (process, refport, value, valueCnt); }


kern_return_t
S_msg_get_env_variable (mach_port_t process,
	string_t variable,
	data_t *value,
	mach_msg_type_number_t *valueCnt)
{ return _S_msg_get_env_variable (process, variable, value, valueCnt); }


kern_return_t
S_msg_set_env_variable (mach_port_t process,
	mach_port_t refport,
	string_t variable,
	string_t value,
	boolean_t replace)
{ return _S_msg_set_env_variable (process, refport, variable, value, replace); }

error_t
S_msg_describe_ports (mach_port_t process,
		      mach_port_t refport,
		      mach_port_array_t names,
		      mach_msg_type_number_t namesCnt,
		      data_t *descriptions,
		      mach_msg_type_number_t *descriptionsCnt)
{
  return _S_msg_describe_ports (process, refport, names, namesCnt,
				descriptions, descriptionsCnt);
}

error_t
S_msg_report_wait (mach_port_t process, thread_t thread,
		   string_t desc, int *rpc)
{
  *desc = 0;
  *rpc = 0;
  return 0;
}
