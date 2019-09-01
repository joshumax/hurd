/* Start and maintain hurd core servers and system run state

   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
     2005, 2008, 2010, 2013 Free Software Foundation, Inc.
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
#include <assert-backtrace.h>
#include <hurd/paths.h>
#include <sys/reboot.h>
#include <sys/file.h>
#include <unistd.h>
#include <string.h>
#include <mach/gnumach.h>
#include <mach/notify.h>
#include <stdlib.h>
#include <hurd/msg.h>
#include <hurd/term.h>
#include <hurd/fshelp.h>
#include <paths.h>
#include <sys/mman.h>
#include <hurd/msg_server.h>
#include <wire.h>
#include <sys/wait.h>
#include <error.h>
#include <hurd/msg_reply.h>
#include <argz.h>
#include <maptime.h>
#include <version.h>
#include <argp.h>
#include <pids.h>
#include <idvec.h>
#include <stdlib.h>

#include "shutdown_U.h"
#include "startup_notify_U.h"
#include "startup_reply_U.h"
#include "startup_S.h"
#include "notify_S.h"
#include "mung_msg_S.h"
#include "fsys_S.h"
#include "fs_S.h"
#include "io_S.h"

/* host_reboot flags for when we crash.  */
static int crash_flags = RB_AUTOBOOT;

static int verbose = 0;

#define BOOT(flags)	((flags & RB_HALT) ? "halt" : "reboot")


const char *argp_program_version = STANDARD_HURD_VERSION (startup);

#define OPT_KERNEL_TASK	-1

#define _SERVERS_SHUTDOWN	_SERVERS	"/shutdown"

static struct argp_option
options[] =
{
  {"single-user", 's', 0, 0, "Startup system in single-user mode"},
  {"query",       'q', 0, 0, "Ask for the names of servers to start"},
  {"init-name",   'n', 0, 0 },
  {"crash-debug",  'H', 0, 0, "On system crash, go to kernel debugger"},
  {"debug",       'd', 0, 0 },
  {"fake-boot",   'f', 0, 0, "This hurd hasn't been booted on the raw machine"},
  {"verbose",     'v', 0, 0, "be verbose"},
  {0,             'x', 0, OPTION_HIDDEN},
  {"kernel-task", OPT_KERNEL_TASK, "PORT"},
  {0}
};

static char doc[] = "Start and maintain hurd core servers and system run state";

static int booted;		/* Set when the core servers are up.  */

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
static struct ess_task *ess_tasks;
static struct ntfy_task *ntfy_tasks;


/* Our receive right */
static mach_port_t startup;

/* Ports to the kernel.  We use alias to the internal glibc locations
   so that other code can get them using get_privileged_ports.  */
#define host_priv	_hurd_host_priv
#define device_master	_hurd_device_master

/* Args to bootstrap, expressed as flags */
static int bootstrap_args = 0;

/* Stored information for returning proc and auth startup messages. */
static mach_port_t procreply, authreply;
static mach_msg_type_name_t procreplytype, authreplytype;

/* Our ports to auth and proc. */
static mach_port_t authserver;
static mach_port_t procserver;

/* Our bootstrap port, on which we call fsys_getpriv and fsys_init. */
static mach_port_t bootport;

/* Set iff we are a `fake' bootstrap. */
static int fakeboot;

/* The tasks of auth and proc and the bootstrap filesystem. */
static task_t authtask, proctask, fstask;
static task_t kernel_task;

static mach_port_t default_ports[INIT_PORT_MAX];
static mach_port_t default_dtable[3];
static int default_ints[INIT_INT_MAX];

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

/* Do an RPC to /servers/shutdown
 * to call platform specific shutdown routine
 */
error_t
do_shutdown (void)
{
  shutdown_t pc;

  pc = file_name_lookup (_SERVERS_SHUTDOWN, O_READ, 0);
  if (! MACH_PORT_VALID (pc))
    return errno;

  shutdown_shutdown (pc);
  return 0;
}

/* Reboot the microkernel.  */
void
reboot_mach (int flags)
{
  if (fakeboot)
    {
      fprintf (stderr, "%s: Would %s Mach with flags %#x\n",
               program_invocation_short_name, BOOT (flags), flags);
      exit (1);
    }
  else
    {
      error_t err;
      sleep (5);
      if (flags & RB_HALT) {
	fprintf (stderr, "%s: %sing Hurd...\n",
	         program_invocation_short_name, BOOT (flags));
	err = do_shutdown ();
	if (err)
	  error (0, err, "shutdown");
	sleep (2);
	fprintf (stderr, "Didn't succeed\n");
      }
      fprintf (stderr, "%s: %sing Mach (flags %#x)...\n",
               program_invocation_short_name, BOOT (flags), flags);
      err = host_reboot (host_priv, flags);
      if (err)
	error (1, err, "reboot");
      for (;;) sleep (1);
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
notify_shutdown (const char *msg)
{
  struct ntfy_task *n;

  for (n = ntfy_tasks; n != NULL; n = n->next)
    {
      error_t err;
      fprintf (stderr, "%s: notifying %s of %s...",
               program_invocation_short_name, n->name, msg);

      err = startup_dosync (n->notify_port, 60000); /* 1 minute to reply */
      if (err == MACH_SEND_INVALID_DEST)
	fprintf (stderr, "(no longer present)\n");
      else if (err)
	fprintf (stderr, "%s\n", strerror (err));
      else
	fprintf (stderr, "done\n");
    }
}

/* Reboot the Hurd. */
void
reboot_system (int flags)
{
  notify_shutdown (BOOT (flags));

  if (fakeboot)
    {
      pid_t *pp;
      size_t npids = 0;
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
	      size_t pisize = 0;
	      char *noise;
	      size_t noise_len = 0;
	      int flags;
	      err = proc_getprocinfo (procserver, pp[ind], &flags,
				      (int **)&pi, &pisize,
				      &noise, &noise_len);
	      if (err == MACH_SEND_INVALID_DEST)
		goto procbad;
	      if (err)
		{
		  error (0, err, "Getting procinfo for pid %d", pp[ind]);
		  continue;
		}
	      if (!(pi->state & PI_NOPARENT))
		{
		  fprintf (stderr, "%s: Killing pid %d\n",
                           program_invocation_short_name, pp[ind]);
		  task_terminate (task);
		}
	      if (noise_len > 0)
		munmap (noise, noise_len);
	    }
	}
      fprintf (stderr, "%s: Killing proc server\n",
               program_invocation_short_name);
      task_terminate (proctask);
      fprintf (stderr, "%s: Exiting", program_invocation_short_name);
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

typedef error_t (*insert_ports_fnc_t) (char **argv, size_t *argv_len, task_t task);

/* Run SERVER, giving it INIT_PORT_MAX initial ports from PORTS.
   Set TASK to be the task port of the new image. */
void
run (const char *server, mach_port_t *ports, task_t *task,
     insert_ports_fnc_t insert_ports)
{
  char buf[BUFSIZ];
  const char *prog = server;

  if (bootstrap_args & RB_INITNAME)
    {
      fprintf (stderr, "Server file name (default %s): ", server);
      if (getstring (buf, sizeof (buf)))
	prog = buf;
    }

  while (1)
    {
      file_t file;
      error_t err;

      file = file_name_lookup (prog, O_EXEC, 0);
      if (file == MACH_PORT_NULL)
	error (0, errno, "%s", prog);
      else
	{
          char *argz = NULL;
          size_t argz_len = 0;
          err = argz_create_sep (prog, ' ', &argz, &argz_len);
          assert_perror_backtrace (err);

          err = task_create (mach_task_self (),
#ifdef KERN_INVALID_LEDGER
                             NULL, 0,	/* OSF Mach */
#endif
                             0, task);
          assert_perror_backtrace (err);

          if (insert_ports)
            {
              err = insert_ports (&argz, &argz_len, *task);
              assert_perror_backtrace (err);
            }

	  if (bootstrap_args & RB_KDB)
	    {
	      fprintf (stderr, "Pausing for %s\n", prog);
	      getchar ();
	    }
#ifdef HAVE_FILE_EXEC_PATHS
	  err = file_exec_paths (file, *task, 0, (char *)prog, (char *)prog,
				 argz,
				 argz_len, /* Args.  */
				 startup_envz, startup_envz_len,
				 default_dtable,
				 MACH_MSG_TYPE_COPY_SEND, 3,
				 ports, MACH_MSG_TYPE_COPY_SEND,
				 INIT_PORT_MAX,
				 default_ints, INIT_INT_MAX,
				 NULL, 0, NULL, 0);
	  /* For backwards compatibility.  Just drop it when we kill
	     file_exec.  */
	  if (err == MIG_BAD_ID)
#endif
	    err = file_exec (file, *task, 0,
			     argz, argz_len, /* Args.  */
			     startup_envz, startup_envz_len,
			     default_dtable, MACH_MSG_TYPE_COPY_SEND, 3,
			     ports, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
			     default_ints, INIT_INT_MAX,
			     NULL, 0, NULL, 0);
	  if (!err)
	    break;

	  error (0, err, "%s", prog);
	}

      fprintf (stderr, "File name for server %s (or nothing to reboot): ",
               server);
      if (getstring (buf, sizeof (buf)))
	prog = buf;
      else
	crash_system ();
    }

  if (verbose)
    fprintf (stderr, "started %s\n", prog);

  /* Dead-name notification on the task port will tell us when it dies,
     so we can crash if we don't make it to a fully bootstrapped Hurd.  */
  request_dead_name (*task);
}

/* Insert PORT of type PORT_TYPE into TASK, adding '--ARGUMENT=<name>'
   to ARGZ (with <name> being the name valid in TASK).  */
error_t
argz_task_insert_right (char **argz, size_t *argz_len, task_t task,
                        const char *argument,
                        mach_port_t port, mach_msg_type_name_t port_type)
{
  error_t err;
  mach_port_t name;
  char *arg;

  name = MACH_PORT_NULL;
  do
    {
      name += 1;
      err = mach_port_insert_right (task, name, port, port_type);
    }
  while (err == KERN_NAME_EXISTS);

  if (asprintf (&arg, "--%s=%lu", argument, name) < 0)
    return errno;

  err = argz_add (argz, argz_len, arg);
  free (arg);
  return err;
}

error_t
proc_insert_ports (char **argz, size_t *argz_len, task_t task)
{
  error_t err;

  if (MACH_PORT_VALID (kernel_task))
    {
      err = argz_task_insert_right (argz, argz_len, task,
                                    "kernel-task",
                                    kernel_task, MACH_MSG_TYPE_COPY_SEND);
      if (err)
        return err;
    }

  return 0;
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
      fprintf (stderr, "File name [%s]: ", filename);
      if (getstring (buf, sizeof (buf)) && *buf)
	filename = buf;
      file = file_name_lookup (filename, O_EXEC, 0);
      if (file == MACH_PORT_NULL)
	error (0, errno, "%s", filename);
    }
  while (file == MACH_PORT_NULL);
#else
  file = file_name_lookup (filename, O_EXEC, 0);
  if (file == MACH_PORT_NULL)
    {
      error (0, errno, "%s", filename);
      return 0;
    }
#endif

  task_create (mach_task_self (),
#ifdef KERN_INVALID_LEDGER
	       NULL, 0,	/* OSF Mach */
#endif
	       0, &task);
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
      fprintf (stderr, "Pausing for %s\n", filename);
      getchar ();
    }
  progname = strrchr (filename, '/');
  if (progname)
    ++progname;
  else
    progname = filename;
#ifdef HAVE_FILE_EXEC_PATHS
  err = file_exec_paths (file, task, 0, filename, filename,
			 args, arglen,
			 startup_envz, startup_envz_len,
			 default_dtable, MACH_MSG_TYPE_COPY_SEND, 3,
			 default_ports, MACH_MSG_TYPE_COPY_SEND,
			 INIT_PORT_MAX,
			 default_ints, INIT_INT_MAX,
			 NULL, 0, NULL, 0);
  /* For backwards compatibility.  Just drop it when we kill file_exec.  */
  if (err == MIG_BAD_ID)
#endif
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

/* XXX: The libc should provide this function.  */
static void
mig_reply_setup (
	const mach_msg_header_t	*in,
	mach_msg_header_t	*out)
{
      static const mach_msg_type_t RetCodeType = {
		/* msgt_name = */		MACH_MSG_TYPE_INTEGER_32,
		/* msgt_size = */		32,
		/* msgt_number = */		1,
		/* msgt_inline = */		TRUE,
		/* msgt_longform = */		FALSE,
		/* msgt_deallocate = */		FALSE,
		/* msgt_unused = */		0
	};

#define	InP	(in)
#define	OutP	((mig_reply_header_t *) out)
      OutP->Head.msgh_bits =
	MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(InP->msgh_bits), 0);
      OutP->Head.msgh_size = sizeof *OutP;
      OutP->Head.msgh_remote_port = InP->msgh_remote_port;
      OutP->Head.msgh_local_port = MACH_PORT_NULL;
      OutP->Head.msgh_seqno = 0;
      OutP->Head.msgh_id = InP->msgh_id + 100;
      OutP->RetCodeType = RetCodeType;
      OutP->RetCode = MIG_BAD_ID;
#undef InP
#undef OutP
}

static int
demuxer (mach_msg_header_t *inp,
	 mach_msg_header_t *outp)
{
  mig_routine_t routine;

  if (verbose > 1)
    error (0, 0, "%d", inp->msgh_id);

  mig_reply_setup (inp, outp);

  if ((routine = notify_server_routine (inp)) ||
      (routine = msg_server_routine (inp)) ||
      (routine = fsys_server_routine (inp)) ||
      (routine = fs_server_routine (inp)) ||
      (routine = io_server_routine (inp)) ||
      (routine = startup_server_routine (inp)))
    {
      (*routine) (inp, outp);

      if (verbose > 1)
        error (0, ((mig_reply_header_t *) outp)->RetCode, "%d", inp->msgh_id);
      return TRUE;
    }
  else
    return FALSE;
}

error_t
install_as_translator (void)
{
  error_t err;
  file_t node;

  node = file_name_lookup (_SERVERS_STARTUP, O_NOTRANS, 0);
  if (! MACH_PORT_VALID (node))
    {
      if (errno == ENOENT)
	{
	  /* Degrade gracefully if the node does not exist.  */
	  error (0, errno, "%s", _SERVERS_STARTUP);
	  return 0;
	}
      return errno;
    }

  err = file_set_translator (node,
			     0, FS_TRANS_SET, 0,
			     NULL, 0,
			     startup, MACH_MSG_TYPE_COPY_SEND);
  mach_port_deallocate (mach_task_self (), node);
  return err;
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
    case 'v': verbose++; break;
    case 'x': /* NOP */ break;
    case OPT_KERNEL_TASK:
      kernel_task = atoi (arg);
      break;
    default: return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

int
main (int argc, char **argv, char **envp)
{
  error_t err;
  int i;
  int flags;
  mach_port_t consdev;
  struct argp argp = { options, parse_opt, 0, doc };

  /* Parse the arguments.  We don't want the vector reordered, we
     should pass on to our child the exact arguments we got and just
     ignore any arguments that aren't flags for us.  ARGP_NO_ERRS
     suppresses --help and --version, so we only use that option if we
     are booting.  */
  flags = ARGP_IN_ORDER;
  if (getpid () == 0)
    flags |= ARGP_NO_ERRS;
  argp_parse (&argp, argc, argv, flags, 0, 0);

  if (getpid () > 0)
    error (2, 0, "can only be run by bootstrap filesystem");

  global_argv = argv;

  /* Fetch a port to the bootstrap filesystem, the host priv and
     master device ports, and the console.  */
  if (task_get_bootstrap_port (mach_task_self (), &bootport)
      || fsys_getpriv (bootport, &host_priv, &device_master, &fstask)
      || device_open (device_master, D_READ|D_WRITE, "console", &consdev))
    crash_mach ();

  /* Clear our bootstrap port so our children don't inherit it.  */
  task_set_bootstrap_port (mach_task_self (), MACH_PORT_NULL);

  stderr = stdout = mach_open_devstream (consdev, "w");
  stdin = mach_open_devstream (consdev, "r");
  if (stdout == NULL || stdin == NULL)
    crash_mach ();
  setbuf (stdout, NULL);

  err = wire_task_self ();
  if (err)
    error (0, err, "wire_task_self");

  err = argz_create (envp, &startup_envz, &startup_envz_len);
  assert_perror_backtrace (err);

  /* At this point we can use assert to check for errors.  */
  err = mach_port_allocate (mach_task_self (),
			    MACH_PORT_RIGHT_RECEIVE, &startup);
  assert_perror_backtrace (err);
  err = mach_port_insert_right (mach_task_self (), startup, startup,
				MACH_MSG_TYPE_MAKE_SEND);
  assert_perror_backtrace (err);

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
  run ("/hurd/proc", default_ports, &proctask, proc_insert_ports);
  if (! verbose)
    fprintf (stderr, " proc");
  run ("/hurd/auth", default_ports, &authtask, NULL);
  if (! verbose)
    fprintf (stderr, " auth");
  default_ports[INIT_PORT_BOOTSTRAP] = MACH_PORT_NULL;

  /* Wait for messages.  When both auth and proc have started, we
     run launch_system which does the rest of the boot.  */
  while (1)
    {
      err = mach_msg_server (demuxer, 0, startup);
      assert_perror_backtrace (err);
    }
}

void
launch_core_servers (void)
{
  mach_port_t old;
  mach_port_t authproc, fsproc, procproc;
  error_t err;

  if (verbose)
    fprintf (stderr, "Launching core servers\n");

  /* Reply to the proc and auth servers.   */
  err = startup_procinit_reply (procreply, procreplytype, 0,
				mach_task_self (), authserver,
				host_priv, MACH_MSG_TYPE_COPY_SEND,
				device_master, MACH_MSG_TYPE_COPY_SEND);
  assert_perror_backtrace (err);
  if (!fakeboot)
    {
      mach_port_deallocate (mach_task_self (), device_master);
      device_master = 0;
    }

  if (verbose)
    fprintf (stderr, "proc launched\n");

  /* Mark us as important.  */
  err = proc_mark_important (procserver);
  assert_perror_backtrace (err);
  err = proc_mark_exec (procserver);
  assert_perror_backtrace (err);
  proc_set_exe (procserver, "/hurd/startup");

  /* Declare that the filesystem and auth are our children. */
  err = proc_child (procserver, fstask);
  assert_perror_backtrace (err);
  err = proc_child (procserver, authtask);
  assert_perror_backtrace (err);

  err = proc_task2proc (procserver, authtask, &authproc);
  assert_perror_backtrace (err);
  err = proc_mark_important (authproc);
  assert_perror_backtrace (err);
  err = proc_mark_exec (authproc);
  assert_perror_backtrace (err);
  proc_set_exe (authproc, "/hurd/auth");

  err = install_as_translator ();
  if (err)
    /* Good luck.  Who knows, maybe it's an old installation.  */
    error (0, err, "Failed to bind to " _SERVERS_STARTUP);

  if (verbose)
    fprintf (stderr, "Installed on /servers/startup\n");

  err = startup_authinit_reply (authreply, authreplytype, 0, authproc,
				MACH_MSG_TYPE_MOVE_SEND);
  assert_perror_backtrace (err);

  if (verbose)
    fprintf (stderr, "auth launched\n");

  /* Give the library our auth and proc server ports.  */
  _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], authserver);
  _hurd_port_set (&_hurd_ports[INIT_PORT_PROC], procserver);

  /* Do NOT run _hurd_proc_init!  That will start signals, which we do not
     want.  We listen to our own message port.  Tell the proc server where
     our args and environment are.  */
  err = proc_set_arg_locations (procserver,
				(vm_address_t) global_argv,
				(vm_address_t) environ);
  assert_perror_backtrace (err);

  default_ports[INIT_PORT_AUTH] = authserver;

  /* Declare that the proc server is our child.  */
  err = proc_child (procserver, proctask);
  assert_perror_backtrace (err);
  err = proc_task2proc (procserver, proctask, &procproc);
  if (!err)
    {
      proc_mark_important (procproc);
      proc_mark_exec (procproc);
      proc_set_exe (procproc, "/hurd/proc");
      mach_port_deallocate (mach_task_self (), procproc);
    }

  err = proc_register_version (procserver, host_priv,
			       "init", "", HURD_VERSION);
  assert_perror_backtrace (err);

  /* Get the bootstrap filesystem's proc server port.
     We must do this before calling proc_setmsgport below.  */
  err = proc_task2proc (procserver, fstask, &fsproc);
  assert_perror_backtrace (err);
  err = proc_mark_important (fsproc);
  assert_perror_backtrace (err);
  err = proc_mark_exec (fsproc);
  assert_perror_backtrace (err);
  proc_set_exe (fsproc, "fs");

  fprintf (stderr, ".\n");

  /* Tell the proc server our msgport.  Be sure to do this after we are all
     done making requests of proc.  Once we have done this RPC, proc
     assumes it can send us requests, so we cannot block on proc again
     before accepting more RPC requests!  However, we must do this before
     calling fsys_init, because fsys_init blocks on exec_init, and
     exec_init will block waiting on our message port.  */
  err = proc_setmsgport (procserver, startup, &old);
  assert_perror_backtrace (err);
  if (old != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), old);

  if (verbose)
    fprintf (stderr, "Message port registered\n");

  /* Give the bootstrap FS its proc and auth ports.  */
  err = fsys_init (bootport, fsproc, MACH_MSG_TYPE_MOVE_SEND, authserver);
  if (err)
    error (0, err, "fsys_init"); /* Not necessarily fatal.  */

  if (verbose)
    fprintf (stderr, "Fixed up bootstrap filesystem\n");
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

  std_port_array = alloca (sizeof (mach_port_t) * INIT_PORT_MAX);
  std_int_array = alloca (sizeof (int) * INIT_INT_MAX);

  memset (std_port_array, 0, sizeof(mach_port_t) * INIT_PORT_MAX);
  memset (std_int_array, 0, sizeof(int) * INIT_INT_MAX);

  __USEPORT (AUTH, auth_makeauth (port, 0, MACH_MSG_TYPE_COPY_SEND, 0,
				  0, 0, 0, 0, 0, 0, 0, 0, &nullauth));

  /* MAKE_SEND is safe in these transactions because we destroy REF
     ourselves each time. */
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
				     MACH_MSG_TYPE_MOVE_SEND, INIT_PORT_MAX,
				     std_int_array, INIT_INT_MAX));
}

/* Frobnicate the kernel task and the proc server's idea of it (PID 2),
   so the kernel command line can be read as for a normal Hurd process.  */

void
dump_processes (void)
{
  pid_t pid;
  for (pid = 1; pid < 100; pid++)
    {
      char args[256], *buffer = args;
      size_t len = sizeof args;
      if (proc_getprocargs (procserver, pid, &buffer, &len) == 0)
        {
          fprintf (stderr, "pid%d\t%s\n", (int) pid, buffer);
          if (buffer != args)
            vm_deallocate (mach_task_self (), (vm_offset_t) buffer, len);
        }
    }
}

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

  if (verbose)
    fprintf (stderr, "Frobbing kernel process\n");

  if (MACH_PORT_VALID (kernel_task))
    {
      task = kernel_task;
      kernel_task = MACH_PORT_NULL;
    }
  else
    {
      err = proc_pid2task (procserver, HURD_PID_KERNEL, &task);
      if (err)
        {
          error (0, err, "cannot get kernel task port");
          return;
        }
    }

  /* Make the kernel our child.  */
  err = proc_child (procserver, task);
  if (err)
    {
      error (0, err, "cannot make the kernel our child");
      dump_processes ();
    }

  err = proc_task2proc (procserver, task, &proc);
  if (err)
    {
      error (0, err, "cannot get kernel task's proc server port");
      mach_port_deallocate (mach_task_self (), task);
      return;
    }

  /* Mark the kernel task as an essential task so that we or the proc server
     never want to task_terminate it.  */
  err = proc_mark_important (proc);
  if (err)
    error (0, err, "cannot mark the kernel as important");

  err = record_essential_task ("kernel", task);
  assert_perror_backtrace (err);

  proc_set_exe (proc, "kernel");

  err = task_get_bootstrap_port (task, &kbs);
  assert_perror_backtrace (err);
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

  /* Our arguments make up the multiboot command line used to boot the
     kernel.  We'll write into the kernel task a page containing a
     canonical argv array and argz of those words.  */

  err = argz_create (&global_argv[1], &argz, &argzlen);
  assert_perror_backtrace (err);
  err = argz_insert (&argz, &argzlen, argz, "gnumach");
  assert_perror_backtrace (err);
  argc = argz_count (argz, argzlen);

  windowsz = round_page (((argc + 1) * sizeof (char *)) + argzlen);

  mine = (vm_address_t) mmap (0, windowsz, PROT_READ|PROT_WRITE,
			      MAP_ANON, 0, 0);
  assert_backtrace (mine != -1);
  err = vm_allocate (task, &his, windowsz, 1);
  if (err)
    {
      error (0, err, "cannot allocate %Zu bytes in kernel task", windowsz);
      free (argz);
      mach_port_deallocate (mach_task_self (), proc);
      mach_port_deallocate (mach_task_self (), task);
      munmap ((caddr_t) mine, windowsz);
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
  munmap ((caddr_t) mine, windowsz);
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

/** Running userland.  **/

/* In the "split-init" setup, we just run a single program (usually
   /libexec/runsystem) that is not expected to ever exit (or stop).
   If it does exit (or can't be started), we go to an emergency single-user
   shell as a fallback.  */


static pid_t child_pid;		/* PID of the child we run */
static task_t child_task;		/* and its (original) task port */

static void launch_something (const char *why);


/* SIGNO has arrived and has been validated.  Do whatever work it
   implies. */
void
process_signal (int signo)
{
  if (signo == SIGCHLD)
    {
      /* A child died.  Find its status.  */
      int status;
      pid_t pid;

      while (1)
	{
	  pid = waitpid (WAIT_ANY, &status, WNOHANG | WUNTRACED);
	  if (pid <= 0)
	    break;		/* No more children.  */

	  /* Since we are init, orphaned processes get reparented to us and
	     alas, all our adopted children eventually die.  Woe is us.  We
	     just need to reap the zombies to relieve the proc server of
	     its burden, and then we can forget about the little varmints.  */

	  if (pid == child_pid)
	    {
	      /* The big magilla bit the dust.  */

	      char *desc = 0;

	      mach_port_deallocate (mach_task_self (), child_task);
	      child_task = MACH_PORT_NULL;
	      child_pid = -1;

	      if (WIFSIGNALED (status))
		asprintf (&desc, "terminated abnormally (%s)",
			  strsignal (WTERMSIG (status)));
	      else if (WIFSTOPPED (status))
		asprintf (&desc, "stopped abnormally (%s)",
			  strsignal (WTERMSIG (status)));
	      else if (WEXITSTATUS (status) == 0)
		desc = strdup ("finished");
	      else
		asprintf (&desc, "exited with status %d",
			  WEXITSTATUS (status));

	      {
		char buf[40];
		snprintf (buf, sizeof buf, "%d", status);
		setenv ("STATUS", buf, 1);
	      }

	      launch_something (desc);
              free (desc);
	    }
	}
    }
}

/* Start the child program PROG.  It is run via /libexec/console-run
   with the given additional arguments.  */
static int
start_child (const char *prog, char **progargs)
{
  file_t file;
  error_t err;
  char *args;
  size_t arglen;

  if (progargs == 0)
    {
      const char *argv[] = { "/libexec/console-run", prog, 0 };
      err = argz_create ((char **) argv, &args, &arglen);
    }
  else
    {
      int argc = 0;
      while (progargs[argc] != 0)
	++argc;
      {
	const char *argv[2 + argc + 1];
	argv[0] = "/libexec/console-run";
	argv[1] = prog;
	argv[2 + argc] = 0;
	while (argc-- > 0)
	  argv[2 + argc] = progargs[argc];
	err = argz_create ((char **) argv, &args, &arglen);
      }
    }
  assert_perror_backtrace (err);

  if (verbose)
    fprintf (stderr, "Going to execute '%s'\n", args);

  file = file_name_lookup (args, O_EXEC, 0);
  if (file == MACH_PORT_NULL)
    {
      error (0, errno, "%s", args);
      free (args);
      return -1;
    }

  task_create (mach_task_self (),
#ifdef KERN_INVALID_LEDGER
	       NULL, 0,	/* OSF Mach */
#endif
	       0, &child_task);
  proc_set_init_task (procserver, child_task);
  proc_task2pid (procserver, child_task, &child_pid);
  proc_task2proc (procserver, child_task, &default_ports[INIT_PORT_PROC]);

  if (bootstrap_args & RB_KDB)
    {
      fprintf (stderr, "Pausing for %s\n", args);
      getchar ();
    }

#ifdef HAVE_FILE_EXEC_PATHS
  err = file_exec_paths (file, child_task, 0, args, args,
			 args, arglen,
			 startup_envz, startup_envz_len,
			 NULL, MACH_MSG_TYPE_COPY_SEND, 0, /* No fds.  */
			 default_ports, MACH_MSG_TYPE_COPY_SEND,
			 INIT_PORT_MAX,
			 default_ints, INIT_INT_MAX,
			 NULL, 0, NULL, 0);
  /* For backwards compatibility.  Just drop it when we kill file_exec.  */
  if (err == MIG_BAD_ID)
#endif
    err = file_exec (file, child_task, 0,
		     args, arglen,
		     startup_envz, startup_envz_len,
		     NULL, MACH_MSG_TYPE_COPY_SEND, 0, /* No fds.  */
		     default_ports, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
		     default_ints, INIT_INT_MAX,
		     NULL, 0, NULL, 0);

  proc_mark_important (default_ports[INIT_PORT_PROC]);
  mach_port_deallocate (mach_task_self (), default_ports[INIT_PORT_PROC]);
  mach_port_deallocate (mach_task_self (), file);
  if (err)
    {
      error (0, err, "Cannot execute %s", args);
      free (args);
      return -1;
    }
  free (args);
  return 0;
}

static void
launch_something (const char *why)
{
  file_t something;
  static unsigned int try;
  static const char *const tries[] =
  {
    "/libexec/runsystem",
    _PATH_BSHELL,
    "/bin/shd",			/* XXX */
  };

  if (why)
    error (0, 0, "%s %s", tries[try - 1], why);

  something = file_name_lookup (tries[try], O_EXEC, 0);
  if (something != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (), something);
      if (try == 0 && start_child (tries[try++], &global_argv[1]) == 0)
        return;
    }
  else
    try++;

  while (try < sizeof tries / sizeof tries[0])
    {
      something = file_name_lookup (tries[try], O_EXEC, 0);
      if (something != MACH_PORT_NULL)
	{
	  mach_port_deallocate (mach_task_self (), something);
	  if (start_child (tries[try++], NULL) == 0)
	    return;
	}
      else
	try++;
    }

  crash_system ();
}

void
launch_system (void)
{
  launch_something (0);

  if (verbose)
    fprintf (stderr, "Init has completed\n");
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

  if (verbose)
    fprintf (stderr, "Received startup message from proc\n");

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

  if (verbose)
    fprintf (stderr, "Received startup message from auth\n");

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

  /* Always deallocate the extra reference this message carries.  */
  if (MACH_PORT_VALID (credential))
    mach_port_deallocate (mach_task_self (), credential);

  if (credential != host_priv)
    return EPERM;

  if (verbose)
    fprintf (stderr, "Received startup essential message from '%s'\n",
             name);

  fail = record_essential_task (name, task);
  if (fail)
    return fail;

  if (!booted)
    {
      if (!strcmp (name, "auth"))
	authinit = 1;
      else if (!strcmp (name, "exec"))
        {
          execinit = 1;
          mach_port_t execproc;
          proc_task2proc (procserver, task, &execproc);
          proc_mark_important (execproc);
          proc_set_exe (execproc, "/hurd/exec");
        }
      else if (!strcmp (name, "proc"))
	procinit = 1;

      if (verbose)
        fprintf (stderr, "  still waiting for:");

      if (authinit && execinit && procinit)
	{
          if (verbose)
            fprintf (stderr, " none!\n");
	  /* Reply to this RPC, after that everything
	     is ready for real startup to begin. */
	  startup_essential_task_reply (reply, replytype, 0);

	  init_stdarrays ();
	  frob_kernel_process ();

	  launch_system ();

	  booted = 1;

	  return MIG_NO_REPLY;
	}

      if (verbose)
        {
          if (! authinit)
            fprintf (stderr, " auth");
          if (! execinit)
            fprintf (stderr, " exec");
          if (! procinit)
            fprintf (stderr, " proc");
          fprintf (stderr, "\n");
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

  assert_backtrace (notify == startup);

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
      error (0, 0, "BUG!  Unexpected dead-name notification (name %#lx)",
	     name);
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
		   string_t desc, mach_msg_id_t *rpc)
{
  *desc = 0;
  *rpc = 0;
  return 0;
}

/* fsys */
error_t
S_fsys_getroot (mach_port_t fsys_t,
		mach_port_t dotdotnode,
		uid_t *uids, size_t nuids,
		uid_t *gids, size_t ngids,
		int flags,
		retry_type *do_retry,
		char *retry_name,
		mach_port_t *ret,
		mach_msg_type_name_t *rettype)
{
  int is_root = 0;
  size_t i;

  for (i = 0; i < nuids; i++)
    if (uids[i] == 0)
      {
        is_root = 1;
        break;
      }

  if (! is_root)
    return EPERM;

  *do_retry = FS_RETRY_NORMAL;
  *retry_name = '\0';
  *ret = startup;
  *rettype = MACH_MSG_TYPE_COPY_SEND;
  return 0;
}

error_t
S_fsys_get_options (mach_port_t control,
		    data_t *data, mach_msg_type_number_t *len)
{
  /* XXX Implement me.  */
  return EOPNOTSUPP;
}

error_t
S_file_check_access (mach_port_t server,
                     int *allowed)
{
  if (server != startup)
    return EOPNOTSUPP;
  *allowed = 0;
  return 0;
}

error_t
S_io_stat (mach_port_t server,
           struct stat *st)
{
  if (server != startup)
    return EOPNOTSUPP;

  memset (st, 0, sizeof *st);

  st->st_fstype = FSTYPE_MISC;
  st->st_fsid = getpid ();
  st->st_mode = S_IFCHR | S_IROOT;

  return 0;
}

error_t
S_io_restrict_auth (mach_port_t server,
                    mach_port_t *newport,
                    mach_msg_type_name_t *newporttype,
                    uid_t *uids, size_t nuids,
                    uid_t *gids, size_t ngids)
{
  struct idvec user = { uids, (unsigned) nuids, (unsigned) nuids };

  if (server != startup)
    return EOPNOTSUPP;

  if (! idvec_contains (&user, 0))
    return EPERM;

  *newport = server;
  *newporttype = MACH_MSG_TYPE_COPY_SEND;
  return 0;
}
