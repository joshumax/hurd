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

#include "startup_reply.h"
#include "startup_S.h"

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
  printf ("init: %sing Mach (flags %#x)...\n", BOOT (flags), flags);
  while (errno = host_reboot (host_priv, flags))
    perror ("host_reboot");
  for (;;);
}

void
crash_mach (void)
{
  reboot_mach (CRASH_FLAGS);
}

void
reboot_system (int flags)
{
  struct ntfy_task *n;

  for (n = ntfy_tasks; n != NULL; n = n->next)
    {
      error_t err;
      printf ("init: notifying %p\n", (void *) n->notify_port);
      /* XXX need to time out on reply */
      err = startup_dosync (n->notify_port);
      if (err && err != MACH_SEND_INVALID_DEST)
	printf ("init: %p complained: %s\n",
		(void *) n->notify_port,
		strerror (err));
    }

  reboot_mach (flags);
}

void
crash (void)
{
  reboot_system (CRASH_FLAGS);
}

/* Run SERVER, giving it INIT_PORT_MAX initial ports from PORTS. 
   Set TASK to be the task port of the new image. */
void
run (char *server, mach_port_t *ports, task_t *task)
{
  error_t err;
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
	  task_create (mach_task_self (), 0, task);
	  printf ("Pausing for %s\n", prog);
	  getchar ();
	  err = file_exec (file, *task, 0,
			   NULL, 0, /* No args.  */
			   NULL, 0, /* No env.  */
			   default_dtable, MACH_MSG_TYPE_COPY_SEND, 3,
			   ports, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
			   NULL, 0, /* No info in init ints.  */
			   NULL, 0, NULL, 0);
	  if (!err)
	    break;

#ifdef notyet
	  hurd_perror (prog, err);
#endif
	}

      printf ("File name for server %s (or nothing to reboot): ", server);
      if (getstring (buf, sizeof (buf)))
	prog = buf;
      else
	crash ();
    }

  printf ("started %s\n", prog);
}

/* Run FILENAME as root. */
void
run_for_real (char *filename)
{
  file_t file;
  error_t err;
  char buf[512];
  task_t task;
  
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

  task_create (mach_task_self (), 0, &task);
  proc_child (procserver, task);
  proc_task2proc (procserver, task, &default_ports[INIT_PORT_PROC]);
  printf ("Pausing for %s\n", filename);
  getchar ();
  err = file_exec (file, task, 0,
		   NULL, 0, /* No args.  */
		   NULL, 0, /* No env.  */
		   default_dtable, MACH_MSG_TYPE_COPY_SEND, 3,
		   default_ports, MACH_MSG_TYPE_COPY_SEND,
		   INIT_PORT_MAX,
		   NULL, 0, /* No info in init ints.  */
		   NULL, 0, NULL, 0);
  mach_port_deallocate (mach_task_self (), default_ports[INIT_PORT_PROC]);
  mach_port_deallocate (mach_task_self (), task);
  mach_port_deallocate (mach_task_self (), file);
}

int
main (int argc, char **argv, char **envp)
{
  extern int startup_server (); /* XXX */
  int err;
  int i;
  mach_port_t consdev;
  
  global_argv = argv;

  /* Fetch a port to the bootstrap filesystem, the host priv and
     master device ports, and the console */
  if (task_get_bootstrap_port (mach_task_self (), &bootport)
      || fsys_getpriv (bootport, &host_priv, &device_master, &fstask)
      || device_open (device_master, D_WRITE, "console", &consdev))
    crash_mach ();

  stdin = mach_open_devstream (consdev, "w+");
  if (stdin == NULL)
    crash_mach ();
  stdout = stderr = stdin;
  setbuf (stdout, NULL);
  
  /* At this point we can use assert to check for errors. */
  err = mach_port_allocate (mach_task_self (),
			    MACH_PORT_RIGHT_RECEIVE, &startup);
  assert (!err);
  err = mach_port_insert_right (mach_task_self (), startup, startup,
				MACH_MSG_TYPE_MAKE_SEND);
  assert (!err);

  /* Set up the set of ports we will pass to the programs we exec. */
  for (i = 0; i < INIT_PORT_MAX; i++)
    switch (i)
      {
      case INIT_PORT_CRDIR:
	default_ports[i] = getcrdir ();
	break;
      case INIT_PORT_CWDIR:
	default_ports[i] = getcwdir ();
	break;
      case INIT_PORT_BOOTSTRAP:
	default_ports[i] = startup;
	break;
      default:
	default_ports[i] = MACH_PORT_NULL;
	break;
      }
  
  default_dtable[0] = getdport (0);
  default_dtable[1] = getdport (1);
  default_dtable[2] = getdport (2);

  run ("/hurd/proc", default_ports, &proctask);
  run ("/hurd/auth", default_ports, &authtask);
  
  /* Wait for messages.  When both auth and proc have started, we
     run launch_system which does the rest of the boot. */
  while (1)
    {
      err = mach_msg_server (startup_server, 0, startup);
      assert (!err);
    }
}

void
launch_system (void)
{
  mach_port_t old;
  mach_port_t authproc, fsproc;
  
  /* Reply to the proc and auth servers.   */
  startup_procinit_reply (procreply, procreplytype, 0, 
			  mach_task_self (), authserver, 
			  host_priv, MACH_MSG_TYPE_COPY_SEND,
			  device_master, MACH_MSG_TYPE_MOVE_SEND);
  device_master = 0;
  proc_task2proc (procserver, authtask, &authproc);
  startup_authinit_reply (authreply, authreplytype, 0, authproc, 
			  MACH_MSG_TYPE_MOVE_SEND);

  /* Give the library our auth and proc server ports.  */
  _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], authserver);
  _hurd_port_set (&_hurd_ports[INIT_PORT_PROC], procserver);

  default_ports[INIT_PORT_AUTH] = authserver;

  /* Tell the proc server our msgport and where our args and
     environment are.  */
  proc_setmsgport (procserver, startup, &old);
  if (old)
    mach_port_deallocate (mach_task_self (), old);
  proc_setprocargs (procserver, (int) global_argv, (int) environ);

  /* Give the bootstrap FS its proc and auth ports.  */
  proc_task2proc (procserver, fstask, &fsproc);
  if (errno =  fsys_init (bootport, fsproc, MACH_MSG_TYPE_MOVE_SEND,
			  authserver))
    perror ("fsys_init");

  /* Declare that the filesystem and auth are our children. */
  proc_child (procserver, fstask);
  proc_child (procserver, authtask);
  
  run_for_real ("/bin/sh");
  printf ("Init has completed.\n");
}


error_t
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
    launch_system ();

  return MIG_NO_REPLY;
}

/* Called by the auth server when it starts up.  */

error_t
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
    launch_system ();

  return MIG_NO_REPLY;
}
    
/* Unimplemented stubs */
error_t
S_startup_essential_task (mach_port_t server,
			  task_t task,
			  mach_port_t excpt,
			  char *name)
{
  return EOPNOTSUPP;
}

error_t
S_startup_request_notification (mach_port_t server,
				mach_port_t notify)
{
  return EOPNOTSUPP;
}

error_t
S_startup_reboot (mach_port_t server,
		  mach_port_t refpt,
		  int code)
{
  return EOPNOTSUPP;
}

