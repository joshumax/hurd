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

/* The tasks of auth and proc and the bootstrap filesystem. */
task_t authtask, prottask, fstask;

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

/* The RPC server demultiplexer.  */
static int
request_server (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  extern int notify_server (), startup_server ();
  
  return (notify_server (inp, outp) ||
	  startup_server (inp, outp));
}

/* Run SERVER, giving it INIT_PORT_MAX initial ports from PORTS.  */
void
run (char *server, mach_port_t *ports)
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
	  err = file_exec (file, MACH_PORT_NULL, EXEC_NEWTASK,
			   NULL, 0, /* No args.  */
			   NULL, 0, /* No env.  */
			   NULL, MACH_MSG_TYPE_COPY_SEND, 0, /* No dtable.  */
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

int
main (int argc, char **argv, char **envp)
{
  int err;
  mach_port_t bootport;
  int i;
  mach_port_t ports[INIT_PORT_MAX];
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
  
  /* Set up the set of ports we will pass to the programs we exec. */
  for (i = 0; i < INIT_PORT_MAX; i++)
    switch (i)
      {
      case INIT_PORT_CRDIR:
	ports[i] = getcrdir ();
	break;
      case INIT_PORT_CWDIR:
	ports[i] = getcwdir ();
	break;
      case INIT_PORT_BOOTSTRAP:
	ports[i] = startup;
	break;
      default:
	ports[i] = MACH_PORT_NULL;
	break;
      }
  
  run ("/hurd/proc", ports, &proctask);
  run ("/hurd/auth", ports, &authtask);
  
  /* Wait for messages.  When both auth and proc have started, we
     run launch_system which does the rest of the boot. */
  while (1)
    {
      err = mach_msg_server (request_server, 0, startup);
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
  _hurd_port_init (&_hurd_ports[INIT_PORT_AUTH], authserver);
  _hurd_port_init (&_hurd_ports[INIT_PORT_PROC], procserver);

  /* Tell the proc server our msgport and where our args and
     environment are.  */
  proc_setmsgport (procserver, startup, &old);
  if (old)
    mach_port_deallocate (mach_task_self (), old);
  proc_setprocargs (procserver, (int) global_argv, (int) environ);

  /* Give the bootstrap FS its proc and auth ports.  */
  {
    fsys_t fsys;

    if (errno = file_getcontrol (getcrdir (), &fsys))
      perror ("file_getcontrol (root)");
    else
      {
	proc_task2proc (procserver, fstask, &fsproc);
	if (errno = fsys_init (fsys, fsproc, MACH_MSG_TYPE_MOVE_SEND,
			       authserver))
	  perror ("fsys_init");
	mach_port_deallocate (mach_task_self (), fsys);
      }
  }
  
  printf ("Init has completed.\n");
}


error_t
S_startup_procinit (startup_t server,
		    mach_port_t reply,
		    mach_msg_type_name_t reply_porttype,
		    process_t proc, 
		    mach_port_t *startuptask,
		    auth_t *auth,
		    mach_port_t *priv, mach_port_t *dev)
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
		    mach_port_t *proc)
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
    
