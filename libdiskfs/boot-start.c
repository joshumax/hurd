/*
   Copyright (C) 1993, 1994, 1995 Free Software Foundation

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
#include <hurd/fsys.h>
#include <stdio.h>
#include <hurd/exec.h>
#include <fcntl.h>
#include <device/device.h>
#include <sys/reboot.h>
#include <string.h>
#include <hurd.h>
#include "fsys_S.h"
#include "fsys_reply_U.h"

mach_port_t diskfs_exec_ctl;
mach_port_t diskfs_exec;
extern task_t diskfs_execserver_task;

static struct mutex execstartlock;
static struct condition execstarted;

static char *default_init = "hurd/init";

static void start_execserver ();

static char **saved_argv;

/* Once diskfs_root_node is set, call this if we are a bootstrap
   filesystem.  */
void
diskfs_start_bootstrap (char **argv)
{
  mach_port_t root_pt, startup_pt, bootpt;
  retry_type retry;
  char pathbuf[1024];
  string_t retry_name;
  uid_t idlist[] = {0, 0, 0};
  mach_port_t portarray[INIT_PORT_MAX];
  mach_port_t fdarray[3];	/* XXX */
  mach_port_t con;		/* XXX */
  task_t newt;
  error_t err;
  char *initname, *initnamebuf;
  char *exec_argv;
  int exec_argvlen;
  struct port_info *bootinfo;

  saved_argv = argv;

  /* Get the execserver going and wait for its fsys_startup */
  mutex_init (&execstartlock);
  condition_init (&execstarted);
  mutex_lock (&execstartlock);
  start_execserver ();
  condition_wait (&execstarted, &execstartlock);
  mutex_unlock (&execstartlock);
  assert (diskfs_exec_ctl);

  /* Create the port for current and root directory.  */
  root_pt = (ports_get_right
	     (diskfs_make_protid
	      (diskfs_make_peropen (diskfs_root_node, O_READ | O_EXEC,
				    MACH_PORT_NULL),
	       0,0,0,0)));
  /* Get us a send right to copy around.  */
  mach_port_insert_right (mach_task_self (), root_pt, root_pt,
			  MACH_MSG_TYPE_MAKE_SEND);

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
  if (diskfs_bootflags & RB_INITNAME)
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

  bootinfo = ports_allocate_port (diskfs_port_bucket,
				  sizeof (struct port_info),
				  diskfs_initboot_class);  
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
  
/* XXX */
  device_open (diskfs_master_device, D_WRITE | D_READ, "console", &con);

  fdarray[0] = con;
  fdarray[1] = con;
  fdarray[2] = con;
/* XXX */
  exec_argvlen = asprintf (&exec_argv, "%s%c%s%c",
			   initname, '\0',
			   diskfs_bootflagarg, '\0');

  err = task_create (mach_task_self (), 0, &newt);
  assert_perror (err);
  if (diskfs_bootflags & RB_KDB)
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
diskfs_S_exec_startup (mach_port_t port,
		       vm_address_t *base_addr,
		       vm_size_t *stack_size,
		       int *flags,
		       char **argvP,
		       u_int *argvlen,
		       char **envpP __attribute__ ((unused)),
		       u_int *envplen,
		       mach_port_t **dtableP,
		       mach_msg_type_name_t *dtablepoly,
		       u_int *dtablelen,
		       mach_port_t **portarrayP,
		       mach_msg_type_name_t *portarraypoly,
		       u_int *portarraylen,
		       int **intarrayP,
		       u_int *intarraylen)
{
  mach_port_t *portarray, *dtable;
/*  char *argv, *envp; */
  mach_port_t rootport;
  device_t con;
  struct ufsport *upt;
  char exec_argv[] = "[BOOT EXECSERVER]";

  if (!(upt = ports_lookup_port (diskfs_port_bucket, port, 
				 diskfs_execboot_class)))
    return EOPNOTSUPP;

  *base_addr = 0;
  *stack_size = 0;

  *flags = 0;
  
  if (*argvlen < sizeof (exec_argv))
    vm_allocate (mach_task_self (),
		 (vm_address_t *) argvP, sizeof (exec_argv), 1);
  bcopy (exec_argv, *argvP, sizeof (exec_argv));
  *argvlen = sizeof (exec_argv);
  
  *envplen = 0;

  if (*portarraylen < INIT_PORT_MAX)
    vm_allocate (mach_task_self (), (u_int *)portarrayP,
		 (INIT_PORT_MAX * sizeof (mach_port_t)), 1);
  portarray = *portarrayP;
  *portarraylen = INIT_PORT_MAX;

  if (*dtablelen < 3)
    vm_allocate (mach_task_self (), (u_int *)dtableP,
		 (3 * sizeof (mach_port_t)), 1);
  dtable = *dtableP;
  *dtablelen = 3;
  
  *intarrayP = NULL;
  *intarraylen = 0;

  rootport = (ports_get_right 
	      (diskfs_make_protid
	       (diskfs_make_peropen (diskfs_root_node, O_READ | O_EXEC,
				     MACH_PORT_NULL),
		0,0,0,0)));

  portarray[INIT_PORT_CWDIR] = rootport;
  portarray[INIT_PORT_CRDIR] = rootport;
  portarray[INIT_PORT_AUTH] = MACH_PORT_NULL;
  portarray[INIT_PORT_PROC] = MACH_PORT_NULL;
  portarray[INIT_PORT_CTTYID] = MACH_PORT_NULL;
  portarray[INIT_PORT_BOOTSTRAP] = port; /* use the same port */

  *portarraypoly = MACH_MSG_TYPE_MAKE_SEND;

/* XXX */
  device_open (diskfs_master_device, D_WRITE | D_READ, "console", &con);

  dtable[0] = con;
  dtable[1] = con;
  dtable[2] = con;
/* XXX */

  *dtablepoly = MACH_MSG_TYPE_COPY_SEND;

  ports_port_deref (upt);
  return 0;
}

/* Called by S_fsys_startup for execserver bootstrap.  The execserver
   is able to function without a real node, hence this fraud.  */
error_t
diskfs_execboot_fsys_startup (mach_port_t port,
			      mach_port_t ctl,
			      mach_port_t *real,
			      mach_msg_type_name_t *realpoly)
{
  struct port_info *pt;
  
  if (!(pt = ports_lookup_port (diskfs_port_bucket, port, 
				diskfs_execboot_class)))
    return EOPNOTSUPP;
  
  *real = MACH_PORT_NULL;
  *realpoly = MACH_MSG_TYPE_COPY_SEND;
  
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
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       mach_port_t *hostpriv,
		       mach_port_t *device_master,
		       mach_port_t *fstask)
{
  struct port_info *pt;
  if (!(pt = ports_lookup_port (diskfs_port_bucket, port, 
				diskfs_initboot_class)))
    return EOPNOTSUPP;
  
  *hostpriv = diskfs_host_priv;
  *device_master = diskfs_master_device;
  *fstask = mach_task_self ();
  ports_port_deref (pt);
  return 0;
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
  error_t err;
  mach_port_t root_pt;

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

  assert (diskfs_execserver_task != MACH_PORT_NULL);
  err = proc_task2proc (procserver, diskfs_execserver_task, &execprocess);
  assert_perror (err);

  /* Declare that the exec server is our child. */
  proc_child (procserver, diskfs_execserver_task);

  /* Don't start this until now so that exec is fully authenticated 
     with proc. */
  exec_init (diskfs_exec, authhandle, execprocess, MACH_MSG_TYPE_MOVE_SEND);

  /* We don't need this anymore. */
  mach_port_deallocate (mach_task_self (), diskfs_execserver_task);
  diskfs_execserver_task = MACH_PORT_NULL;

  /* Get a port to the root directory to put in the library's
     data structures.  */
  root_pt = (ports_get_right
	     (diskfs_make_protid
	      (diskfs_make_peropen (diskfs_root_node, O_READ|O_WRITE|O_EXEC,
				    MACH_PORT_NULL),
	       0,0,0,0)));
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
      _hurd_proc_init (saved_argv);
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
      _hurd_init (0, saved_argv, portarray, INIT_PORT_MAX, NULL, 0);
    }  

  diskfs_init_completed ();

  return MIG_NO_REPLY;		/* Already replied above.  */
}

/* Unused */
error_t
diskfs_S_exec_init (mach_port_t a __attribute__ ((unused)),
		    auth_t b __attribute__ ((unused)),
		    process_t c __attribute__ ((unused)))
{
  return EOPNOTSUPP;
}

/* Unused */
error_t
diskfs_S_exec_setexecdata (mach_port_t a __attribute__ ((unused)),
			   mach_port_t *b __attribute__ ((unused)),
			   u_int c __attribute__ ((unused)),
			   int bcopy __attribute__ ((unused)),
			   int *d __attribute__ ((unused)),
			   u_int e __attribute__ ((unused)),
			   int ecopy __attribute__ ((unused)))
{
  return EOPNOTSUPP;
}

/* Unused. */
error_t
diskfs_S_exec_exec (mach_port_t execserver __attribute__ ((unused)),
		    mach_port_t file __attribute__ ((unused)),
		    mach_port_t oldtask __attribute__ ((unused)),
		    int flags __attribute__ ((unused)),
		    data_t argv __attribute__ ((unused)),
		    mach_msg_type_number_t argvCnt __attribute__ ((unused)),
		    boolean_t argvSCopy __attribute__ ((unused)),
		    data_t envp __attribute__ ((unused)),
		    mach_msg_type_number_t envpCnt __attribute__ ((unused)),
		    boolean_t envpSCopy __attribute__ ((unused)),
		    portarray_t dtable __attribute__ ((unused)),
		    mach_msg_type_number_t dtableCnt __attribute__ ((unused)),
		    boolean_t dtableSCopy __attribute__ ((unused)),
		    portarray_t portarray __attribute__ ((unused)),
		    mach_msg_type_number_t portarrayCnt 
		      __attribute__ ((unused)),
		    boolean_t portarraySCopy __attribute__ ((unused)),
		    intarray_t intarray __attribute__ ((unused)),
		    mach_msg_type_number_t intarrayCnt 
		      __attribute__ ((unused)),
		    boolean_t intarraySCopy __attribute__ ((unused)),
		    mach_port_t *deallocnames __attribute__ ((unused)),
		    u_int deallocnamescnt __attribute__ ((unused)),
		    mach_port_t *destroynames __attribute__ ((unused)),
		    u_int destroynamescnt __attribute__ ((unused))
		    )
{
  return EOPNOTSUPP;
}

/* Unused. */
error_t
diskfs_S_exec_boot_init (mach_port_t execserver __attribute__ ((unused)),
			 startup_t init __attribute__ ((unused)))
{
  return EOPNOTSUPP;
}

/* Start the execserver running (when we are a bootstrap filesystem).  */
static void 
start_execserver (void)
{
  mach_port_t right;
  extern task_t diskfs_execserver_task;	/* Set in boot-parse.c.  */
  struct port_info *execboot_info;

  assert (diskfs_execserver_task != MACH_PORT_NULL);

  execboot_info = ports_allocate_port (diskfs_port_bucket,
				       sizeof (struct port_info),
				       diskfs_execboot_class);
  right = ports_get_right (execboot_info)
  mach_port_insert_right (mach_task_self (), right,
			  right, MACH_MSG_TYPE_MAKE_SEND);
  ports_port_deref (execboot_info);
  task_set_special_port (diskfs_execserver_task, TASK_BOOTSTRAP_PORT, right);
  mach_port_deallocate (mach_task_self (), right);

  if (diskfs_bootflags & RB_KDB)
    {
      printf ("pausing for exec\n");
      getc (stdin);
    }
  task_resume (diskfs_execserver_task);

  printf (" exec");
  fflush (stdout);
}

