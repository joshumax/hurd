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

int
main (int argc, char **argv)
{
  int err;
  mach_port_t bootport;
  
  /* Fetch a port to the bootstrap filesystem, the host priv and
     master device ports, and the console */
  if (task_get_bootstrap_port (mach_task_self (), &bootport)
      || fsys_getpriv (bootport, &host_priv, &device_master)
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
  
  run (_HURD_PROC, ports);
  run (_HURD_AUTH, ports);
  
  /* Wait for messages.  When both auth and proc have started, we
     run launch_system which does the rest of the boot. */
  while (1)
    {
      err = mach_msg_server (request_server, 0, startup);
      assert (!err);
    }
}

error_t
launch_system (void)
{
  mach_port_t old;
  
  /* Reply to the proc and auth servers.  */
  startup_procinit_reply (procreply, procreplytype, 0, authserver, host_priv,
			  device_master);
  startup_authinit_reply (authreply, authreplytype, 0, authserver_proc);

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
	if (errno = fsys_init (fsys, bootfs_proc, authserver))
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
		    process_t proc, process_t fs_proc, process_t auth_proc,
		    auth_t *auth,
		    mach_port_t *priv, mach_port_t *dev)
{
  if (procserver)
    /* Only one proc server.  */
    return EPERM;

  procserver = proc;

  bootfs_proc = fs_proc;
  authserver_proc = auth_proc;

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
		    mach_port_t auth)
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
    
