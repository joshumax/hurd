/*
   Copyright (C) 1993, 1994, 1995, 1996 Free Software Foundation

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

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include <stdio.h>
#include <hurd.h>
#include <hurd/fsys.h>
#include <hurd/exec.h>
#include <hurd/startup.h>
#include <hurd/paths.h>
#include <fcntl.h>
#include <device/device.h>
#include <sys/reboot.h>
#include <string.h>
#include "fsys_S.h"
#include "fsys_reply_U.h"

mach_port_t diskfs_exec_ctl;
mach_port_t diskfs_exec;
extern task_t diskfs_exec_server_task;

static struct mutex execstartlock;
static struct condition execstarted;

static char *default_init = "hurd/init";

static void start_execserver ();

char **diskfs_argv = 0;

static mach_port_t
get_console ()
{
  mach_port_t device_master, console;
  error_t err = get_privileged_ports (0, &device_master);

  if (err)
    return MACH_PORT_NULL;

  err = device_open (device_master, D_WRITE | D_READ, "console", &console);
  if (err)
    return MACH_PORT_NULL;

  return console;
}

/* Once diskfs_root_node is set, call this if we are a bootstrap
   filesystem.  */
void
diskfs_start_bootstrap ()
{
  mach_port_t root_pt, startup_pt, bootpt;
  retry_type retry;
  char pathbuf[1024];
  string_t retry_name;
  uid_t idlist[] = {0, 0, 0};
  mach_port_t portarray[INIT_PORT_MAX];
  mach_port_t fdarray[3];	/* XXX */
  task_t newt;
  error_t err;
  char *initname, *initnamebuf;
  char *exec_argv;
  int exec_argvlen;
  struct port_info *bootinfo;
  struct protid *rootpi;

  printf ("Hurd server bootstrap: %s", program_invocation_short_name);
  fflush (stdout);

  /* Get the execserver going and wait for its fsys_startup */
  mutex_init (&execstartlock);
  condition_init (&execstarted);
  mutex_lock (&execstartlock);
  start_execserver ();
  condition_wait (&execstarted, &execstartlock);
  mutex_unlock (&execstartlock);
  assert (diskfs_exec_ctl);

  /* Create the port for current and root directory.  */
  err = diskfs_create_protid (diskfs_make_peropen (diskfs_root_node,
						   O_READ | O_EXEC,
						   MACH_PORT_NULL),
			       0,0,0,0, &rootpi);
  assert_perror (err);
  root_pt = ports_get_right (rootpi);

  /* Get us a send right to copy around.  */
  mach_port_insert_right (mach_task_self (), root_pt, root_pt,
			  MACH_MSG_TYPE_MAKE_SEND);

  ports_port_deref (rootpi);

  /* Contact the exec server.  */
  err = fsys_getroot (diskfs_exec_ctl, root_pt, MACH_MSG_TYPE_COPY_SEND,
		      idlist, 3, idlist, 3, 0,
		      &retry, retry_name, &diskfs_exec);
  assert_perror (err);
  assert (retry == FS_RETRY_NORMAL);
  assert (retry_name[0] == '\0');
  assert (diskfs_exec);


  /* Execute the startup server.  */
  initnamebuf = NULL;
  initname = default_init;
  if (index (diskfs_boot_flags, 'i'))
    {
      size_t bufsz;
      ssize_t len;
      printf ("Init name [%s]: ", default_init);
      bufsz = 0;
      switch (len = getline (&initnamebuf, &bufsz, stdin))
	{
	case -1:
	  perror ("getline");
	  printf ("Using default of `%s'.\n", initname);
	case 0:			/* Hmm.  */
	case 1:			/* Empty line, just a newline.  */
	  /* Use default.  */
	  break;
	default:
	  initnamebuf[len - 1] = '\0'; /* Remove the newline.  */
	  initname = initnamebuf;
	  while (*initname == '/')
	    initname++;
	  break;
	}
    }
  else
    initname = default_init;

  err = dir_lookup (root_pt, initname, O_READ, 0,
		    &retry, pathbuf, &startup_pt);

  assert_perror (err);
  assert (retry == FS_RETRY_NORMAL);
  assert (pathbuf[0] == '\0');

  err = ports_create_port (diskfs_initboot_class, diskfs_port_bucket,
			   sizeof (struct port_info), &bootinfo);
  assert_perror (err);
  bootpt = ports_get_right (bootinfo);
  mach_port_insert_right (mach_task_self (), bootpt, bootpt,
			  MACH_MSG_TYPE_MAKE_SEND);
  ports_port_deref (bootinfo);

  portarray[INIT_PORT_CRDIR] = root_pt;
  portarray[INIT_PORT_CWDIR] = root_pt;
  portarray[INIT_PORT_AUTH] = MACH_PORT_NULL;
  portarray[INIT_PORT_PROC] = MACH_PORT_NULL;
  portarray[INIT_PORT_CTTYID] = MACH_PORT_NULL;
  portarray[INIT_PORT_BOOTSTRAP] = bootpt;

  fdarray[0] = fdarray[1] = fdarray[2] = get_console (); /* XXX */

  exec_argvlen =
    asprintf (&exec_argv, "%s%c%s%c", initname, '\0', diskfs_boot_flags, '\0');

  err = task_create (mach_task_self (), 0, &newt);
  assert_perror (err);
  if (index (diskfs_boot_flags, 'd'))
    {
      printf ("pausing for init...\n");
      getc (stdin);
    }
  printf (" init");
  fflush (stdout);
  err = exec_exec (diskfs_exec, startup_pt, MACH_MSG_TYPE_COPY_SEND,
		   newt, 0, exec_argv, exec_argvlen, 0, 0,
		   fdarray, MACH_MSG_TYPE_COPY_SEND, 3,
		   portarray, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX,
		   /* Supply no intarray, since we have no info for it.
		      With none supplied, it will use the defaults.  */
		   NULL, 0, 0, 0, 0, 0);
  free (exec_argv);
  mach_port_deallocate (mach_task_self (), root_pt);
  mach_port_deallocate (mach_task_self (), startup_pt);
  mach_port_deallocate (mach_task_self (), bootpt);
  if (initnamebuf != default_init)
    free (initnamebuf);
  assert_perror (err);
}

/* We look like an execserver to the execserver itself; it makes this
   call (as does any task) to get its state.  We can't give it all of
   its ports (we'll provide those with a later call to exec_init).  */
kern_return_t
diskfs_S_exec_startup_get_info (mach_port_t port,
				vm_address_t *user_entry,
				vm_address_t *phdr_data,
				vm_size_t *phdr_size,
				vm_address_t *base_addr,
				vm_size_t *stack_size,
				int *flags,
				char **argvP,
				mach_msg_type_number_t *argvlen,
				char **envpP __attribute__ ((unused)),
				mach_msg_type_number_t *envplen,
				mach_port_t **dtableP,
				mach_msg_type_name_t *dtablepoly,
				mach_msg_type_number_t *dtablelen,
				mach_port_t **portarrayP,
				mach_msg_type_name_t *portarraypoly,
				mach_msg_type_number_t *portarraylen,
				int **intarrayP,
				mach_msg_type_number_t *intarraylen)
{
  error_t err;
  mach_port_t *portarray, *dtable;
  mach_port_t rootport;
  struct ufsport *upt;
  struct protid *rootpi;

  if (!(upt = ports_lookup_port (diskfs_port_bucket, port,
				 diskfs_execboot_class)))
    return EOPNOTSUPP;

  *user_entry = 0;
  *phdr_data = *base_addr = 0;
  *phdr_size = *stack_size = 0;

  /* We have no args for it.  Tell it to look on its stack
     for the args placed there by the boot loader.  */
  *argvlen = *envplen = 0;
  *flags = EXEC_STACK_ARGS;

  if (*portarraylen < INIT_PORT_MAX)
    vm_allocate (mach_task_self (), (vm_address_t *) portarrayP,
		 (INIT_PORT_MAX * sizeof (mach_port_t)), 1);
  portarray = *portarrayP;
  *portarraylen = INIT_PORT_MAX;

  if (*dtablelen < 3)
    vm_allocate (mach_task_self (), (vm_address_t *) dtableP,
		 (3 * sizeof (mach_port_t)), 1);
  dtable = *dtableP;
  *dtablelen = 3;
  dtable[0] = dtable[1] = dtable[2] = get_console (); /* XXX */

  *intarrayP = NULL;
  *intarraylen = 0;

  err = diskfs_create_protid (diskfs_make_peropen (diskfs_root_node,
						   O_READ | O_EXEC,
						   MACH_PORT_NULL),
			      0,0,0,0, &rootpi);
  assert_perror (err);
  rootport = ports_get_right (rootpi);
  ports_port_deref (rootpi);
  portarray[INIT_PORT_CWDIR] = rootport;
  portarray[INIT_PORT_CRDIR] = rootport;
  portarray[INIT_PORT_AUTH] = MACH_PORT_NULL;
  portarray[INIT_PORT_PROC] = MACH_PORT_NULL;
  portarray[INIT_PORT_CTTYID] = MACH_PORT_NULL;
  portarray[INIT_PORT_BOOTSTRAP] = port; /* use the same port */

  *portarraypoly = MACH_MSG_TYPE_MAKE_SEND;

  *dtablepoly = MACH_MSG_TYPE_COPY_SEND;

  ports_port_deref (upt);
  return 0;
}

/* Called by S_fsys_startup for execserver bootstrap.  The execserver
   is able to function without a real node, hence this fraud.  */
error_t
diskfs_execboot_fsys_startup (mach_port_t port, int flags,
			      mach_port_t ctl,
			      mach_port_t *real,
			      mach_msg_type_name_t *realpoly)
{
  error_t err;
  string_t pathbuf;
  enum retry_type retry;
  struct port_info *pt;
  struct protid *rootpi;
  mach_port_t rootport;

  if (!(pt = ports_lookup_port (diskfs_port_bucket, port,
				diskfs_execboot_class)))
    return EOPNOTSUPP;

  err = diskfs_create_protid (diskfs_make_peropen (diskfs_root_node, flags,
						   MACH_PORT_NULL),
			      0,0,0,0, &rootpi);
  assert_perror (err);
  rootport = ports_get_right (rootpi);
  mach_port_insert_right (mach_task_self (), rootport, rootport,
			  MACH_MSG_TYPE_MAKE_SEND);
  ports_port_deref (rootpi);

  err = dir_lookup (rootport, _SERVERS_EXEC, flags|O_NOTRANS, 0,
		    &retry, pathbuf, real);
  assert_perror (err);
  assert (retry == FS_RETRY_NORMAL);
  assert (pathbuf[0] == '\0');
  *realpoly = MACH_MSG_TYPE_MOVE_SEND;

  mach_port_deallocate (mach_task_self (), rootport);

  diskfs_exec_ctl = ctl;

  mutex_lock (&execstartlock);
  condition_signal (&execstarted);
  mutex_unlock (&execstartlock);
  ports_port_deref (pt);
  return 0;
}

/* Called by init to get the privileged ports as described
   in <hurd/fsys.defs>. */
kern_return_t
diskfs_S_fsys_getpriv (mach_port_t port,
		       mach_port_t reply, mach_msg_type_name_t reply_type,
		       mach_port_t *host_priv, mach_msg_type_name_t *hp_type,
		       mach_port_t *dev_master, mach_msg_type_name_t *dm_type,
		       mach_port_t *fstask, mach_msg_type_name_t *task_type)
{
  error_t err;
  struct port_info *init_bootstrap_port =
    ports_lookup_port (diskfs_port_bucket, port, diskfs_initboot_class);

  if (!init_bootstrap_port)
    return EOPNOTSUPP;

  err = get_privileged_ports (host_priv, dev_master);
  if (!err)
    {
      *fstask = mach_task_self ();
      *hp_type = *dm_type = MACH_MSG_TYPE_MOVE_SEND;
      *task_type = MACH_MSG_TYPE_COPY_SEND;
    }

  ports_port_deref (init_bootstrap_port);

  return err;
}

/* Called by init to give us ports to the procserver and authserver as
   described in <hurd/fsys.defs>. */
kern_return_t
diskfs_S_fsys_init (mach_port_t port,
		    mach_port_t reply, mach_msg_type_name_t replytype,
		    mach_port_t procserver,
		    mach_port_t authhandle)
{
  struct port_infe *pt;
  static int initdone = 0;
  process_t execprocess;
  string_t version;
  mach_port_t host, startup;
  error_t err;
  mach_port_t root_pt;
  struct protid *rootpi;

  pt = ports_lookup_port (diskfs_port_bucket, port, diskfs_initboot_class);
  if (!pt)
    return EOPNOTSUPP;
  ports_port_deref (pt);
  if (initdone)
    return EOPNOTSUPP;
  initdone = 1;

  /* init is single-threaded, so we must reply to its RPC before doing
     anything which might attempt to send an RPC to init.  */
  fsys_init_reply (reply, replytype, 0);

  /* Allocate our reference here; _hurd_init will consume a reference
     for the library itself. */
  err = mach_port_mod_refs (mach_task_self (),
			    authhandle, MACH_PORT_RIGHT_SEND, +1);
  assert_perror (err);

  if (diskfs_auth_server_port != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), diskfs_auth_server_port);
  diskfs_auth_server_port = authhandle;

  assert (diskfs_exec_server_task != MACH_PORT_NULL);
  err = proc_task2proc (procserver, diskfs_exec_server_task, &execprocess);
  assert_perror (err);

  /* Declare that the exec server is our child. */
  proc_child (procserver, diskfs_exec_server_task);
  proc_mark_exec (execprocess);

  /* Don't start this until now so that exec is fully authenticated
     with proc. */
  exec_init (diskfs_exec, authhandle, execprocess, MACH_MSG_TYPE_MOVE_SEND);

  /* We don't need this anymore. */
  mach_port_deallocate (mach_task_self (), diskfs_exec_server_task);
  diskfs_exec_server_task = MACH_PORT_NULL;

  /* Get a port to the root directory to put in the library's
     data structures.  */
  err = diskfs_create_protid (diskfs_make_peropen (diskfs_root_node,
						   O_READ|O_EXEC,
						   MACH_PORT_NULL),
			      0,0,0,0, &rootpi);
  assert_perror (err);
  root_pt = ports_get_right (rootpi);
  ports_port_deref (rootpi);

  /* We need two send rights, for the crdir and cwdir slots.  */
  mach_port_insert_right (mach_task_self (), root_pt, root_pt,
			  MACH_MSG_TYPE_MAKE_SEND);
  mach_port_mod_refs (mach_task_self (), root_pt,
		      MACH_PORT_RIGHT_SEND, +1);

  if (_hurd_ports)
    {
      /* We already have a portarray, because somebody responded to
	 exec_startup on our initial bootstrap port, even though we are
	 supposedly the bootstrap program.  The losing `boot' that runs on
	 UX does this.  */
      _hurd_port_set (&_hurd_ports[INIT_PORT_PROC], procserver); /* Consume. */
      _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], authhandle); /* Consume. */
      _hurd_port_set (&_hurd_ports[INIT_PORT_CRDIR], root_pt); /* Consume. */
      _hurd_port_set (&_hurd_ports[INIT_PORT_CWDIR], root_pt); /* Consume. */
      _hurd_proc_init (diskfs_argv);
    }
  else
    {
      /* We have no portarray or intarray because there was
	 no exec_startup data; _hurd_init was never called.
	 We now have the crucial ports, so create a portarray
	 and call _hurd_init.  */
      mach_port_t *portarray;
      unsigned int i;
      __vm_allocate (__mach_task_self (), (vm_address_t *) &portarray,
		     INIT_PORT_MAX * sizeof *portarray, 1);
      if (MACH_PORT_NULL != (mach_port_t) 0)
	for (i = 0; i < INIT_PORT_MAX; ++i)
	  portarray[i] = MACH_PORT_NULL;
      portarray[INIT_PORT_PROC] = procserver;
      portarray[INIT_PORT_AUTH] = authhandle;
      portarray[INIT_PORT_CRDIR] = root_pt;
      portarray[INIT_PORT_CWDIR] = root_pt;
      _hurd_init (0, diskfs_argv, portarray, INIT_PORT_MAX, NULL, 0);
    }

  err = get_privileged_ports (&host, 0);
  if (err)
    return err;

  sprintf (version, "%d.%d", diskfs_major_version, diskfs_minor_version);
  proc_register_version (procserver, host,
			 diskfs_server_name, HURD_RELEASE, version);

  err = proc_getmsgport (procserver, 1, &startup);
  if (!err)
    {
      startup_essential_task (startup, mach_task_self (), MACH_PORT_NULL,
			      diskfs_server_name, host);
      mach_port_deallocate (mach_task_self (), startup);
    }

  mach_port_deallocate (mach_task_self (), host);

  _diskfs_init_completed ();

  return MIG_NO_REPLY;		/* Already replied above.  */
}

/* Start the execserver running (when we are a bootstrap filesystem).  */
static void
start_execserver (void)
{
  error_t err;
  mach_port_t right;
  extern task_t diskfs_exec_server_task; /* Set in opts-std-startup.c.  */
  struct port_info *execboot_info;

  assert (diskfs_exec_server_task != MACH_PORT_NULL);

  err = ports_create_port (diskfs_execboot_class, diskfs_port_bucket,
			   sizeof (struct port_info), &execboot_info);
  assert_perror (err);
  right = ports_get_right (execboot_info);
  mach_port_insert_right (mach_task_self (), right,
			  right, MACH_MSG_TYPE_MAKE_SEND);
  ports_port_deref (execboot_info);
  task_set_special_port (diskfs_exec_server_task, TASK_BOOTSTRAP_PORT, right);
  mach_port_deallocate (mach_task_self (), right);

  if (index (diskfs_boot_flags, 'd'))
    {
      printf ("pausing for exec\n");
      getc (stdin);
    }
  task_resume (diskfs_exec_server_task);

  printf (" exec");
  fflush (stdout);
}

