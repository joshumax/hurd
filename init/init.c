/* Init that only bootstraps the hurd and runs sh.
   Copyright (C) 1993, 1994, 1995, 1996 Free Software Foundation, Inc.

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

#include "startup_notify_U.h"
#include "startup_reply_U.h"
#include "startup_S.h"
#include "notify_S.h"
#include "msg_S.h"

/* host_reboot flags for when we crash.  */
#define CRASH_FLAGS	RB_AUTOBOOT

#define BOOT(flags)	((flags & RB_HALT) ? "halt" : "reboot")

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

/* Our receive right */
mach_port_t startup;

/* Ports to the kernel */
mach_port_t host_priv, device_master;

/* Args to bootstrap, expressed as flags */
int bootstrap_args;

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

char *init_version = "0.0";

mach_port_t default_ports[INIT_PORT_MAX];
mach_port_t default_dtable[3];

char **global_argv;

pid_t shell_pid;		/* PID of single-user shell.  */

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

/* Reboot the microkernel.  */
void
reboot_mach (int flags)
{
  if (fakeboot)
    {
      printf ("init: Would %s Mach with flags %#x\n", BOOT (flags), flags);
      fflush (stdout);
      exit (1);
    }
  else
    {
      printf ("init: %sing Mach (flags %#x)...\n", BOOT (flags), flags);
      fflush (stdout);
      while ((errno = host_reboot (host_priv, flags)))
	perror ("host_reboot");
      for (;;);
    }
}

/* Reboot the microkernel, specifying that this is a crash. */
void
crash_mach (void)
{
  reboot_mach (CRASH_FLAGS);
}

/* Reboot the Hurd. */
void
reboot_system (int flags)
{
  struct ntfy_task *n;

  for (n = ntfy_tasks; n != NULL; n = n->next)
    {
      error_t err;
      printf ("%s: notifying %s of shutdown...", 
	      program_invocation_short_name, n->name);
      fflush (stdout);
      err = startup_dosync (n->notify_port, 60000); /* 1 minute to reply */
      if (err == MACH_SEND_INVALID_DEST)
	printf ("(no longer present)\n");
      else if (err)
	printf ("%s", strerror (err));
      else
	printf ("done\n");
      fflush (stdout);
    }

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
	  printf ("Init: can't simulate crash; proc has died\n");
	  fflush (stdout);
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
	      printf ("init: getting task for pid %d: %s\n",
		      pp[ind], strerror (err));
	      fflush (stdout);
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
		  printf ("init: getting procinfo for pid %d: %s\n",
			  pp[ind], strerror (err));
		  fflush (stdout);
		  continue;
		}
	      if (!(pi->state & PI_NOPARENT))
		{
		  printf ("init: killing pid %d\n", pp[ind]);
		  fflush (stdout);
		  task_terminate (task);
		}
	      if (noise_len > 0)
		vm_deallocate (mach_task_self (),
			       (vm_address_t)noise, noise_len);
	    }
	}
      printf ("Killing proc server\n");
      fflush (stdout);
      task_terminate (proctask);
      printf ("Init exiting\n");
      fflush (stdout);
    }
  reboot_mach (flags);
}

/* Reboot the Hurd, specifying that this is a crash. */
void
crash_system (void)
{
  reboot_system (CRASH_FLAGS);
}

/* Run SERVER, giving it INIT_PORT_MAX initial ports from PORTS.
   Set TASK to be the task port of the new image. */
void
run (char *server, mach_port_t *ports, task_t *task)
{
  char buf[BUFSIZ];
  char *prog = server;

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
	perror (prog);
      else
	{
	  char *progname;
	  task_create (mach_task_self (), 0, task);
	  if (bootstrap_args & RB_KDB)
	    {
	      printf ("Pausing for %s\n", prog);
	      getchar ();
	    }
	  progname = strrchr (prog, '/');
	  if (progname)
	    ++progname;
	  else
	    progname = prog;
	  errno = file_exec (file, *task, 0,
			     progname, strlen (progname) + 1, /* Args.  */
			     "", 1, /* No env.  */
			     default_dtable, MACH_MSG_TYPE_COPY_SEND, 3,
			     ports, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
			     NULL, 0, /* No info in init ints.  */
			     NULL, 0, NULL, 0);
	  if (!errno)
	    break;

	  perror (prog);
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
}

/* Run FILENAME as root with ARGS as its argv (length ARGLEN).
   Return the task that we started.  If CTTY is set, then make
   that the controlling terminal of the new process and put it in
   its own login collection.  */
task_t
run_for_real (char *filename, char *args, int arglen, mach_port_t ctty)
{
  file_t file;
  error_t err;
  task_t task;
  char *progname;

#if 0
  char buf[512];
  do
    {
      printf ("File name [%s]: ", filename);
      if (getstring (buf, sizeof (buf)) && *buf)
	filename = buf;
      file = file_name_lookup (filename, O_EXEC, 0);
      if (!file)
	perror (filename);
    }
  while (!file);
#else
  file = file_name_lookup (filename, O_EXEC, 0);
  if (!file)
    {
      perror (filename);
      return MACH_PORT_NULL;
    }
#endif

  task_create (mach_task_self (), 0, &task);
  proc_child (procserver, task);
  proc_task2proc (procserver, task, &default_ports[INIT_PORT_PROC]);
  proc_mark_exec (default_ports[INIT_PORT_PROC]);
  proc_setsid (default_ports[INIT_PORT_PROC]);
  if (ctty != MACH_PORT_NULL)
    {
      int pid;
      term_getctty (ctty, &default_ports[INIT_PORT_CTTYID]);
      proc_task2pid (procserver, task, &pid);
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
		   NULL, 0, /* No env.  */
		   default_dtable, MACH_MSG_TYPE_COPY_SEND, 3,
		   default_ports, MACH_MSG_TYPE_COPY_SEND,
		   INIT_PORT_MAX,
		   NULL, 0, /* No info in init ints.  */
		   NULL, 0, NULL, 0);
  mach_port_deallocate (mach_task_self (), default_ports[INIT_PORT_PROC]);
  if (ctty != MACH_PORT_NULL)
    {
      mach_port_deallocate (mach_task_self (),
			    default_ports[INIT_PORT_CTTYID]);
      default_ports[INIT_PORT_CTTYID] = MACH_PORT_NULL;
    }
  mach_port_deallocate (mach_task_self (), file);
  if (err)
    {
      fprintf (stderr, "Cannot execute %s: %s.\n", filename, strerror (err));
      return MACH_PORT_NULL;
    }
  return task;
}

static int
demuxer (mach_msg_header_t *inp,
	 mach_msg_header_t *outp)
{
  extern int notify_server (), startup_server (), msg_server ();

  return (notify_server (inp, outp) ||
	  msg_server (inp, outp) ||
	  startup_server (inp, outp));
}

int
main (int argc, char **argv, char **envp)
{
  volatile int err;
  int i;
  mach_port_t consdev;

  global_argv = argv;

  /* Parse the arguments */
  bootstrap_args = 0;
  if (argc >= 2)
    {
      if (index (argv[1], 'q'))
	bootstrap_args |= RB_ASKNAME;
      if (index (argv[1], 's'))
	bootstrap_args |= RB_SINGLE;
      if (index (argv[1], 'd'))
	bootstrap_args |= RB_KDB;
      if (index (argv[1], 'n'))
	bootstrap_args |= RB_INITNAME;
      if (index (argv[1], 'f'))
	fakeboot = 1;
    }

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

  /* At this point we can use assert to check for errors.  */
  err = mach_port_allocate (mach_task_self (),
			    MACH_PORT_RIGHT_RECEIVE, &startup);
  assert_perror (err);
  err = mach_port_insert_right (mach_task_self (), startup, startup,
				MACH_MSG_TYPE_MAKE_SEND);
  assert_perror (err);

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
  mach_port_t authproc, fsproc;

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

  proc_register_version (procserver, host_priv, "init", HURD_RELEASE,
			 init_version);

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
    perror ("fsys_init");	/* Not necessarily fatal.  */
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
				     MACH_MSG_TYPE_MOVE_SEND, INIT_PORT_MAX,
				     std_int_array, INIT_INT_MAX));
}


/* Start the single-user environment.  This can only be done
   when the core servers have fully started.  We know that
   startup_essential_task is the last thing they do before being
   ready to handle requests, so we start this once all the necessary
   servers have identified themselves that way. */
void
launch_single_user ()
{
  char shell[1024];
  char terminal[] = "/hurd/term\0/tmp/console\0device\0console";
  mach_port_t term, shelltask;
  char *termname;
  int fd;
  struct stat st;
  error_t err;

  init_stdarrays ();

  printf ("Single-user environment:");
  fflush (stdout);

  /* Open the console.  If we get something which is a terminal, then
     we conclude that the filesystem has a proper translator for it,
     and we're done.  Otherwise, start /hurd/term on something inside
     /tmp and use that.  */
  termname = _PATH_CONSOLE;
  term = file_name_lookup (termname, O_READ, 0);
  if (term != MACH_PORT_NULL)
    {
      err = io_stat (term, &st);
      if (err)
	perror (termname);
    }

  if (term == MACH_PORT_NULL || err || st.st_fstype != FSTYPE_TERM)
    /* Start the terminal server ourselves. */
    {
      mach_port_t control;	/* Control port for term translator.  */
      error_t open_node (int flags,
			 mach_port_t *underlying,
			 mach_msg_type_name_t *underlying_type)
	{
	  term = file_name_lookup (termname, flags | O_CREAT|O_NOTRANS, 0666);
	  if (term == MACH_PORT_NULL)
	    {
	      perror (termname);
	      return errno;
	    }

	  *underlying = term;
	  *underlying_type = MACH_MSG_TYPE_COPY_SEND;

	  return 0;
	}

      termname = terminal + strlen (terminal) + 1; /* first arg is name */

      /* The callback to start_translator opens TERM as a side effect.  */
      errno =
	fshelp_start_translator (open_node,
				 terminal, terminal, sizeof terminal, 3000,
				 &control);
      if (errno)
	{
	  perror (terminal);
	  goto fail;
	}

      errno = file_set_translator (term, 0, FS_TRANS_SET, 0, 0, 0,
				   control, MACH_MSG_TYPE_MOVE_SEND);
      if (errno)
	{
	  perror (termname);
	  goto fail;
	}
      mach_port_deallocate (mach_task_self (), term);

      /* Now repeat the open. */
      term = file_name_lookup (termname, O_READ, 0);
      if (term == MACH_PORT_NULL)
	{
	  perror (termname);
	  goto fail;
	}
      errno = io_stat (term, &st);
      if (errno)
	{
	  perror (termname);
	  term = MACH_PORT_NULL;
	  goto fail;
	}
      if (st.st_fstype != FSTYPE_TERM)
	{
	  fprintf (stderr, "Installed /tmp/console terminal failed\n");
	  term = MACH_PORT_NULL;
	  goto fail;
	}
    }
 fail:

  /* At this point either TERM is the console or it's null.  If it's
     null, then don't do anything, and our fd's will be copied.
     Otherwise, open fd's 0, 1, and 2. */
  if (term != MACH_PORT_NULL)
    {
      fd = open (termname, O_RDWR);
      assert (fd != -1);
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

#if 0
  printf ("Shell program [%s]: ", _PATH_BSHELL);
  if (! getstring (shell, sizeof shell))
#endif
    strcpy (shell, _PATH_BSHELL);

  /* The shell needs a real controlling terminal, so set that up here. */
  shelltask = run_for_real (shell, shell, strlen (shell) + 1, term);
  mach_port_deallocate (mach_task_self (), term);
  if (shelltask != MACH_PORT_NULL)
    {
      shell_pid = task2pid (shelltask);
      mach_port_deallocate (mach_task_self (), shelltask);
    }
  printf (" shell.\n");
  fflush (stdout);
}


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
  struct ess_task *et;
  mach_port_t prev;
  static int authinit, procinit, execinit, initdone;

  if (credential != host_priv)
    return EPERM;
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
  mach_port_request_notification (mach_task_self (), task,
				  MACH_NOTIFY_DEAD_NAME, 1, startup,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &prev);
  if (prev)
    mach_port_deallocate (mach_task_self (), prev);

#if 0
  /* Taking over the exception port will give us a better chance
     if the task tries to get wedged on a fault.  */
  task_set_special_port (task, TASK_EXCEPTION_PORT, startup);
#endif

  mach_port_deallocate (mach_task_self (), credential);

  if (!initdone)
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

	  launch_single_user ();
	  initdone = 1;
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
  mach_port_t prev;

  mach_port_request_notification (mach_task_self (), notify,
				  MACH_NOTIFY_DEAD_NAME, 1, startup,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &prev);
  if (prev != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), prev);

  nt = malloc (sizeof (struct ntfy_task));
  nt->notify_port = notify;
  nt->next = ntfy_tasks;
  ntfy_tasks = nt;
  nt->name = malloc (strlen (name) + 1);
  strcpy (nt->name, nt->name);
  return 0;
}

kern_return_t
do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t name)
{
  struct ntfy_task *nt, *pnt;
  struct ess_task *et;

  for (et = ess_tasks; et != NULL; et = et->next)
    if (et->task_port == name)
      /* An essential task has died.  */
      {
	printf ("Init crashing system; essential task %s died\n",
		et->name);
	fflush (stdout);
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
			 int signo, mach_port_t refport)
{
  if (refport != mach_task_self ())
    return EPERM;

  switch (signo)
    {
    case SIGCHLD:
      {
	/* A child died.  Find its status.  */
	int status;
	pid_t pid = waitpid (WAIT_ANY, &status, WNOHANG);
	if (pid < 0)
	  perror ("init: waitpid");
	else if (pid == 0)
	  fprintf (stderr, "init: Spurious SIGCHLD.\n");
	else if (pid == shell_pid)
	  {
	    fprintf (stderr,
		     "init: Single-user shell PID %d died (%d), restarting.\n",
		     pid, status);
	    launch_single_user ();
	  }
#if 0
	else
	  fprintf (stderr, "init: Random child PID %d died (%d).\n",
		   pid, status);
#endif
	break;
      }

    default:
      break;
    }

  mach_port_deallocate (mach_task_self (), refport);
  return 0;
}

kern_return_t
S_msg_sig_post (mach_port_t msgport,
		mach_port_t reply, mach_msg_type_name_t reply_type,
		int signo, mach_port_t refport)
{
  return S_msg_sig_post_untraced (msgport, reply, reply_type, signo, refport);
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

kern_return_t
S_msg_get_exec_flags (mach_port_t process, mach_port_t refport, int *flags)
{ return _S_msg_get_exec_flags (process, refport, flags); }
kern_return_t
S_msg_set_all_exec_flags (mach_port_t process, mach_port_t refport, int flags)
{ return _S_msg_set_all_exec_flags (process, refport, flags); }
kern_return_t
S_msg_set_some_exec_flags (mach_port_t process, mach_port_t refport, int flags)
{ return _S_msg_set_some_exec_flags (process, refport, flags); }
kern_return_t
S_msg_clear_some_exec_flags (mach_port_t process, mach_port_t refport,
			     int flags)
{ return _S_msg_clear_some_exec_flags (process, refport, flags); }

error_t
S_msg_report_wait (mach_port_t process, thread_t thread,
		   string_t desc, int *rpc)
{
  *desc = 0;
  *rpc = 0;
  return 0;
}
