/*
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


static struct mutex execstartlock;
static struct condition execstarted;

static task_t exectask;
static vm_address_t exec_stack_base;
static vm_size_t exec_stack_size;

static char *default_init = "hurd/init";

static void start_execserver ();

/* Once diskfs_root_node is set, call this if we are a bootstrap
   filesystem.  */
void
diskfs_start_bootstrap (void)
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
  char *argv;
  int argvlen;

  /* Get the execserver going and wait for its fsys_startup */
  mutex_init (&execstartlock);
  condition_init (&execstarted);
  mutex_lock (&execstartlock);
  start_execserver ();
  condition_wait (&execstarted, &execstartlock);
  mutex_unlock (&execstartlock);
  assert (diskfs_exec_ctl);

  /* Create the port for current and root directory */
  root_pt = (ports_get_right
	     (diskfs_make_protid
	      (diskfs_make_peropen (diskfs_root_node, O_READ | O_EXEC,
				    MACH_PORT_NULL),
	       0,0,0,0)));
  mach_port_insert_right (mach_task_self (), root_pt, root_pt,
			  MACH_MSG_TYPE_MAKE_SEND);


  /* Contact the exec server */
  err = fsys_getroot (diskfs_exec_ctl, root_pt, MACH_MSG_TYPE_COPY_SEND,
		      idlist, 3, idlist, 3, 0, 
		      &retry, retry_name, &diskfs_exec);
  assert (!err);
  assert (retry == FS_RETRY_NONE);
  assert (diskfs_exec);

  
  /* Execute the startup server. */
  if (diskfs_bootflags & RB_INITNAME)
    {
      printf ("Init name [%s]: ", default_init);
      fflush (stdout);
      scanf ("%as\n", &initnamebuf);
      initname = initnamebuf;
      if (*initname)
	while (*initname == '/')
	  initname++;
      else
	initname = default_init;
    }
  else
    initnamebuf = initname = default_init;
  
  err = dir_pathtrans (root_pt, initname, O_READ, 0,
		       &retry, pathbuf, &startup_pt);
  
  assert (!err);
  assert (retry == FS_RETRY_NONE);

  bootpt = ports_get_right (ports_allocate_port (sizeof (struct port_info), 
						 PT_INITBOOT));
  mach_port_insert_right (mach_task_self (), bootpt, bootpt,
			  MACH_MSG_TYPE_MAKE_SEND);

  
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
  argvlen = asprintf (&argv, "%s%c%s%c", initname, '\0', diskfs_bootflagarg,
		      '\0');
  
  task_create (mach_task_self (), 0, &newt);
  printf ("pausing for init...\n");
  getc (stdin);
  err = exec_exec (diskfs_exec, startup_pt, MACH_MSG_TYPE_COPY_SEND,
		   newt, 0, argv, argvlen, 0, 0, 
		   fdarray, MACH_MSG_TYPE_COPY_SEND, 3,
		   portarray, MACH_MSG_TYPE_COPY_SEND, INIT_PORT_MAX, 
		   /* Supply no intarray, since we have no info for it.
		      With none supplied, it will use the defaults.  */
		   NULL, 0, 0, 0, 0, 0);
  free (argv);
  mach_port_deallocate (mach_task_self (), root_pt);
  mach_port_deallocate (mach_task_self (), startup_pt);
  mach_port_deallocate (mach_task_self (), bootpt);
  if (initnamebuf != default_init)
    free (initnamebuf);
  assert (!err);
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
		       char **envpP,
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

  if (!(upt = ports_check_port_type (port, PT_EXECBOOT)))
    return EOPNOTSUPP;

  *base_addr = exec_stack_base;
  *stack_size = exec_stack_size;

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

  ports_done_with_port (upt);
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
  
  if (!(pt = ports_check_port_type (port, PT_EXECBOOT)))
    return EOPNOTSUPP;
  
  *real = MACH_PORT_NULL;
  *realpoly = MACH_MSG_TYPE_COPY_SEND;
  
  diskfs_exec_ctl = ctl;

  mutex_lock (&execstartlock);
  condition_signal (&execstarted);
  mutex_unlock (&execstartlock);
  ports_done_with_port (pt);
  return 0;
}
  
/* Called by init to get the privileged ports as described
   in <hurd/fsys.defs>. */
kern_return_t
diskfs_S_fsys_getpriv (mach_port_t port,
		       mach_port_t *hostpriv,
		       mach_port_t *device_master,
		       mach_port_t *fstask)
{
  struct port_info *pt;
  if (!(pt = ports_check_port_type (port, PT_INITBOOT)))
    return EOPNOTSUPP;
  
  *hostpriv = diskfs_host_priv;
  *device_master = diskfs_master_device;
  *fstask = mach_task_self ();
  ports_done_with_port (pt);
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

  pt = ports_check_port_type (port, PT_INITBOOT);
  if (!pt)
    return EOPNOTSUPP;
  ports_done_with_port (pt);
  if (initdone)
    return EOPNOTSUPP;
  initdone = 1;
  
  /* init is single-threaded, so we must reply to its RPC before doing
     anything which might attempt to send an RPC to init.  */
  fsys_init_reply (reply, replytype, 0);

  _hurd_port_set (&_hurd_ports[INIT_PORT_PROC], procserver);
  _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], authhandle);

  if (diskfs_auth_server_port != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), diskfs_auth_server_port);
  diskfs_auth_server_port = authhandle;

  assert (exectask);
  err = proc_task2proc (procserver, exectask, &execprocess);
  assert (!err);

  /* Declare that the exec server is our child. */
  proc_child (procserver, exectask);

  /* Don't start this until now so that exec is fully authenticated 
     with proc. */
  exec_init (diskfs_exec, authhandle, execprocess, MACH_MSG_TYPE_MOVE_SEND);

  /* We don't need this anymore. */
  mach_port_deallocate (mach_task_self (), exectask);
  exectask = MACH_PORT_NULL;

  diskfs_init_completed ();

  return MIG_NO_REPLY;		/* Already replied above.  */
}

/* Unused */
error_t
diskfs_S_exec_init (mach_port_t a,
		    auth_t b,
		    process_t c)
{
  return EOPNOTSUPP;
}

/* Unused */
error_t
diskfs_S_exec_setexecdata (mach_port_t a,
			   mach_port_t *b,
			   u_int c, int bcopy,
			   int *d, u_int e, int ecopy)
{
  return EOPNOTSUPP;
}

/* Unused. */
error_t
diskfs_S_exec_exec (mach_port_t execserver,
		    mach_port_t file,
		    mach_port_t oldtask,
		    int flags,
		    data_t argv,
		    mach_msg_type_number_t argvCnt,
		    boolean_t argvSCopy,
		    data_t envp,
		    mach_msg_type_number_t envpCnt,
		    boolean_t envpSCopy,
		    portarray_t dtable,
		    mach_msg_type_number_t dtableCnt,
		    boolean_t dtableSCopy,
		    portarray_t portarray,
		    mach_msg_type_number_t portarrayCnt,
		    boolean_t portarraySCopy,
		    intarray_t intarray,
		    mach_msg_type_number_t intarrayCnt,
		    boolean_t intarraySCopy,
		    mach_port_t *deallocnames,
		    u_int deallocnamescnt,
		    mach_port_t *destroynames,
		    u_int destroynamescnt
		    )
{
  return EOPNOTSUPP;
}

/* Unused. */
error_t
diskfs_S_exec_boot_init (mach_port_t execserver,
			 startup_t init)
{
  return EOPNOTSUPP;
}

/* Start the execserver running (when we are a bootstrap filesystem).
   CON will eventually go away; right now it's the console device. */
static void 
start_execserver (void)
{
  task_t newt;
  thread_t newthd;
  vm_address_t buf;
  vm_size_t bufsiz;
  vm_address_t bssloc, textloc;
  volatile error_t err;
  mach_port_t right;

  extern int execserver_text_size, execserver_data_size, execserver_bss_size;
  extern int execserver_start;
  extern int execserver_text, execserver_data;

  /* This just sets up and runs the execserver task.  It will do an
     exec_startup (as every new task does) to get other stuff. */

  err = task_create (mach_task_self (), 0, &newt);
  exectask = newt;

  right = ports_get_right (ports_allocate_port (sizeof (struct port_info),
						PT_EXECBOOT));
  mach_port_insert_right (mach_task_self (), right,
			  right, MACH_MSG_TYPE_MAKE_SEND);
  task_set_special_port (newt, TASK_BOOTSTRAP_PORT, right);
  mach_port_deallocate (mach_task_self (), right);

  bufsiz = round_page (execserver_text_size + execserver_data_size);

  err = vm_allocate (mach_task_self (), &buf, bufsiz, 1);
  bcopy (&execserver_text, (char *)buf, execserver_text_size);
  bcopy (&execserver_data, (char *)(buf + execserver_text_size),
	 execserver_data_size);
  textloc = 0x10000;
  err = vm_allocate (newt, &textloc, bufsiz, 0);
  err = vm_write (newt, 0x10000, buf, bufsiz);
  err = vm_deallocate (mach_task_self (), buf, bufsiz);
  err = vm_protect (newt, 0, trunc_page (execserver_text_size) + 0x10000, 0,
	      VM_PROT_READ | VM_PROT_EXECUTE);

  bssloc = 0x10000 + bufsiz;
  err = vm_allocate (newt, &bssloc, round_page (execserver_bss_size), 0);

  err = thread_create (newt, &newthd);
  err = mach_setup_thread (newt, newthd, (void *) execserver_start,
			   &exec_stack_base, &exec_stack_size);
  printf ("pausing for exec\n");
  getc (stdin);
  thread_resume (newthd);
}

