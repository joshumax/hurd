/* Init that only bootstraps the hurd and runs sh.
   Copyright (C) 1993, 1994 Free Software Foundation

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

#include "startup_reply_U.h"
#include "startup_S.h"
#include "notify_S.h"

/* Define this if we should really reboot mach instead of
   just simulating it. */
#undef STANDALONE

/* host_reboot flags for when we crash.  */
#define CRASH_FLAGS	RB_AUTOBOOT

#define BOOT(flags)	((flags & RB_HALT) ? "halt" : "reboot")

/* This structure keeps track of each notified task.  */
struct ntfy_task
  {
    mach_port_t notify_port;
    struct ntfy_task *next;
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

int prompt_for_servers = 0;

/* Our receive right */
mach_port_t startup;

/* Ports to the kernel */
mach_port_t host_priv, device_master;

/* Stored information for returning proc and auth startup messages. */
mach_port_t procreply, authreply;
mach_msg_type_name_t procreplytype, authreplytype;

/* Our ports to auth and proc. */
mach_port_t authserver;
mach_port_t procserver;

/* Our bootstrap port, on which we call fsys_getpriv and fsys_init. */
mach_port_t bootport;

/* The tasks of auth and proc and the bootstrap filesystem. */
task_t authtask, proctask, fstask;

char *init_version = "0.0 pre-alpha";

mach_port_t default_ports[INIT_PORT_MAX];
mach_port_t default_dtable[3];

char **global_argv;


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
#ifdef STANDALONE
  printf ("init: %sing Mach (flags %#x)...\n", BOOT (flags), flags);
  fflush (stdout);
  while (errno = host_reboot (host_priv, flags))
    perror ("host_reboot");
  for (;;);
#else
  printf ("init: Would %s Mach with flags %#x\n", BOOT (flags), flags);
  fflush (stdout);
  exit (1);
#endif  
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
      printf ("init: notifying %p\n", (void *) n->notify_port);
      fflush (stdout);
      /* XXX need to time out on reply */
      err = startup_dosync (n->notify_port);
      if (err && err != MACH_SEND_INVALID_DEST)
	{
	  printf ("init: %p complained: %s\n",
		  (void *) n->notify_port,
		  strerror (err));
	  fflush (stdout);
	}
    }

#ifdef STANDALONE  
  reboot_mach (flags);
#else
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
	    err = proc_getprocinfo (procserver, pp[ind], (int **)&pi, &pisize);
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
	  }
      }
    printf ("Killing proc server\n");
    fflush (stdout);
    task_terminate (proctask);
    printf ("Init exiting\n");
    fflush (stdout);
    exit (1);
  }
#endif
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

  if (prompt_for_servers)
    {
      printf ("Server file name (default %s): ", server);
      if (getstring (buf, sizeof (buf)))
	prog = buf;
    }

  while (1)
    {
      file_t file;

      file = path_lookup (prog, O_EXEC, 0);
      if (file == MACH_PORT_NULL)
	perror (prog);
      else
	{
	  char *progname;
	  task_create (mach_task_self (), 0, task);
	  printf ("Pausing for %s\n", prog);
	  getchar ();
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

  printf ("started %s\n", prog);
  fflush (stdout);
}

/* Run FILENAME as root with ARGS as its argv (length ARGLEN).
   Return the task that we started. */
task_t
run_for_real (char *filename, char *args, int arglen)
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
      file = path_lookup (filename, O_EXEC, 0);
      if (!file)
	perror (filename);
    }
  while (!file);
#else
  file = path_lookup (filename, O_EXEC, 0);
  if (!file)
    {
      perror (filename);
      return MACH_PORT_NULL;
    }
#endif
  
  task_create (mach_task_self (), 0, &task);
  proc_child (procserver, task);
  proc_task2proc (procserver, task, &default_ports[INIT_PORT_PROC]);
  printf ("Pausing for %s\n", filename);
  getchar ();
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
  mach_port_deallocate (mach_task_self (), file);
  return err ? MACH_PORT_NULL : task;
}

static int
demuxer (mach_msg_header_t *inp,
	 mach_msg_header_t *outp)
{
  extern int notify_server (), startup_server ();
  
  return (notify_server (inp, outp) ||
	  startup_server (inp, outp));
}

int
main (int argc, char **argv, char **envp)
{
  int err;
  int i;
  mach_port_t consdev;
  
  global_argv = argv;

  /* Fetch a port to the bootstrap filesystem, the host priv and
     master device ports, and the console.  */
  if (task_get_bootstrap_port (mach_task_self (), &bootport)
      || fsys_getpriv (bootport, &host_priv, &device_master, &fstask)
      || device_open (device_master, D_WRITE, "console", &consdev))
    crash_mach ();

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
  assert (!err);
  err = mach_port_insert_right (mach_task_self (), startup, startup,
				MACH_MSG_TYPE_MAKE_SEND);
  assert (!err);

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
  run ("/hurd/auth", default_ports, &authtask);
  default_ports[INIT_PORT_BOOTSTRAP] = MACH_PORT_NULL;
  
  /* Wait for messages.  When both auth and proc have started, we
     run launch_system which does the rest of the boot.  */
  while (1)
    {
      err = mach_msg_server (demuxer, 0, startup);
      assert (!err);
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
#ifdef STANDALONE
  mach_port_deallocate (mach_task_self (), device_master);
  device_master = 0;
#endif

  /* Declare that the filesystem and auth are our children. */
  proc_child (procserver, fstask);
  proc_child (procserver, authtask);

  proc_task2proc (procserver, authtask, &authproc);
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

#if 0
  printf ("Init has completed.\n");
  fflush (stdout);
#endif

  /* Tell the proc server our msgport.  Be sure to do this after we are all
     done making requests of proc.  Once we have done this RPC, proc
     assumes it can send us requests, so we cannot block on proc again
     before accepting more RPC requests!  However, we must do this before
     calling fsys_init, because fsys_init blocks on exec_init, and
     exec_init to block waiting on our message port.  */
  proc_setmsgport (procserver, startup, &old);
  if (old)
    mach_port_deallocate (mach_task_self (), old);

  /* Give the bootstrap FS its proc and auth ports.  */
  if (errno = fsys_init (bootport, fsproc, MACH_MSG_TYPE_MOVE_SEND,
			 authserver))
    perror ("fsys_init");
}

/* Set up the initial value of the standard exec data. */
void
init_stdarrays ()
{
  auth_t nullauth;
  mach_port_t pt;
  int pid = getpid ();
  mach_port_t *std_port_array;
  int *std_int_array;
  
  std_port_array = alloca (sizeof (mach_port_t) * INIT_PORT_MAX);
  std_int_array = alloca (sizeof (int) * INIT_INT_MAX);
  
  bzero (std_port_array, sizeof (mach_port_t) * INIT_PORT_MAX);
  bzero (std_int_array, sizeof (int) * INIT_INT_MAX);
  
  __USEPORT (AUTH, auth_makeauth (port, 0, MACH_MSG_TYPE_COPY_SEND, 0,
				  0, 0, 0, 0, 0, 0, 0, 0, &nullauth));
  
  pt = getcwdir ();
  io_reauthenticate (pt, pid);
  auth_user_authenticate (nullauth, pt, pid, &std_port_array[INIT_PORT_CWDIR]);
  mach_port_deallocate (mach_task_self (), pt);
  
  pt = getcrdir ();
  io_reauthenticate (pt, pid);
  auth_user_authenticate (nullauth, pt, pid, &std_port_array[INIT_PORT_CRDIR]);
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
  char shell[] = "/bin/sh";
  char pipes[] = "/hurd/pipes\0/servers/socket/1";
  char terminal[] = "/hurd/term\0console\0/dev/console";
  char devname[] = "/hurd/dev\0/dev";
  mach_port_t term;
  task_t termtask;
  task_t foo;
  int fd;
  volatile int run_dev=1;	/* So you can set this from gdb.  */

  init_stdarrays ();

  printf ("Hit t for term, else dev: ");
  fflush (stdout);
  run_dev = getchar () != 't';

  if (run_dev)
    /* Run the device server */
    termtask = run_for_real (devname, devname, sizeof (devname));
  else  
    /* Run the terminal driver and open it for the shell. */
    termtask = run_for_real (terminal, terminal, sizeof (terminal));

  if (termtask)
    {
      /* Open the console.  We are racing here against the
	 terminal driver that we just started, so keep trying until
	 we get it. */
      struct stat st;
      for (;;)
	{
	  term = path_lookup ("/dev/console", O_READ|O_WRITE, 0);
	  if (term)
	    io_stat (term, &st);
	  if (term && st.st_fstype == FSTYPE_TERM)
	    {
	      /* Open the terminal for real */
	      mach_port_deallocate (mach_task_self (), term);
	      
	      /* Open console for our use  */
	      fd = open ("/dev/console", O_READ);
	      assert (fd != -1);
	      dup2 (fd, 0);
	      close (fd);
	      fd = open ("/dev/console", O_WRITE);
	      assert (fd != -1);
	      dup2 (fd, 1);
	      dup2 (fd, 2);
	      close (fd);

	      fclose (stdin);
	      stdin = fdopen (0, "r");

	      /* Set ports in init_dtable for programs we start */
	      mach_port_deallocate (mach_task_self (), default_dtable[0]);
	      mach_port_deallocate (mach_task_self (), default_dtable[1]);
	      mach_port_deallocate (mach_task_self (), default_dtable[2]);
	      default_dtable[0] = getdport (0);
	      default_dtable[1] = getdport (1);
	      default_dtable[2] = getdport (2);
	      break;
	    }
	  /* Perhaps the terminal driver died? */
	  task_suspend (termtask);
	  errno = task_resume (termtask);
	  if (errno == MACH_SEND_INVALID_DEST
	      || errno == KERN_INVALID_ARGUMENT)
	    break;
	}
    }
  if (termtask != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), termtask);

  /* Run the shell.  We must do this before calling proc_setmsgport below,
     because run_for_real does proc server operations.  */
  foo = run_for_real (shell, shell, sizeof (shell));
  if (foo != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), foo);

  /* Run pipes. */
  foo = run_for_real (pipes, pipes, sizeof (pipes));
  if (foo != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), foo);
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
				mach_port_t notify)
{
  struct ntfy_task *nt;
  mach_port_t prev;

  mach_port_request_notification (mach_task_self (), notify, 
				  MACH_NOTIFY_DEAD_NAME, 1, startup, 
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &prev);
  if (prev)
    mach_port_deallocate (mach_task_self (), prev);

  nt = malloc (sizeof (struct ntfy_task));
  nt->notify_port = notify;
  nt->next = ntfy_tasks;
  ntfy_tasks = nt;
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
