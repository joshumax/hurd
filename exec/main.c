/* GNU Hurd standard exec server, main program and server mechanics.

   Copyright (C) 1992,93,94,95,96,97,98,99,2000,01,02,13
   	Free Software Foundation, Inc.
   Written by Roland McGrath.
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

#include "priv.h"
#include <error.h>
#include <device/device.h>
#include <hurd/paths.h>
#include <hurd/startup.h>
#include <argp.h>
#include <argz.h>
#include <version.h>
#include <pids.h>

const char *argp_program_version = STANDARD_HURD_VERSION (exec);

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_allow_open = 0;

/* Our port classes.  */
struct port_class *trivfs_protid_class;
struct port_class *trivfs_control_class;

struct trivfs_control *fsys;

char **save_argv;
mach_port_t opt_device_master;


#include "exec_S.h"
#include "exec_startup_S.h"

static int
exec_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = exec_server_routine (inp)) ||
      (routine = NULL, trivfs_demuxer (inp, outp)) ||
      (routine = exec_startup_server_routine (inp)))
    {
      if (routine)
        (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}


/* Clean up the storage in BOOT, which was never used.  */

void
deadboot (void *p)
{
  struct bootinfo *boot = p;
  size_t i;

  munmap (boot->argv, boot->argvlen);
  munmap (boot->envp, boot->envplen);

  for (i = 0; i < boot->dtablesize; ++i)
    mach_port_deallocate (mach_task_self (), boot->dtable[i]);
  for (i = 0; i < boot->nports; ++i)
    mach_port_deallocate (mach_task_self (), boot->portarray[i]);
  munmap (boot->portarray, boot->nports * sizeof (mach_port_t));
  munmap (boot->intarray, boot->nints * sizeof (int));

  /* See if we are going away and this was the last thing keeping us up.  */
  if (ports_count_class (trivfs_control_class) == 0)
    {
      /* We have no fsys control port, so we are detached from the
	 parent filesystem.  Maybe we have no users left either.  */
      if (ports_count_class (trivfs_protid_class) == 0)
	{
	  /* We have no user ports left.  Are we still listening for
	     exec_startup RPCs from any tasks we already started?  */
	  if (ports_count_class (execboot_portclass) == 0)
	    /* Nobody talking.  Time to die.  */
	    exit (0);
	  ports_enable_class (execboot_portclass);
	}
      ports_enable_class (trivfs_protid_class);
    }
  ports_enable_class (trivfs_control_class);
}

#define OPT_DEVICE_MASTER_PORT	(-1)

static const struct argp_option options[] =
{
  {"device-master-port", OPT_DEVICE_MASTER_PORT, "PORT", 0,
   "If specified, a boot-time exec server can print "
   "diagnostic messages earlier.", 0},
  {0}
};

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    default:
      return ARGP_ERR_UNKNOWN;
    case ARGP_KEY_INIT:
    case ARGP_KEY_SUCCESS:
    case ARGP_KEY_ERROR:
      break;

    case OPT_DEVICE_MASTER_PORT:
      opt_device_master = atoi (arg);
      break;
    }
  return 0;
}

/* This will be called from libtrivfs to help construct the answer
   to an fsys_get_options RPC.  */
error_t
trivfs_append_args (struct trivfs_control *fsys,
		    char **argz, size_t *argz_len)
{
  error_t err = 0;
  char *opt;

  if (MACH_PORT_VALID (opt_device_master))
    {
      asprintf (&opt, "--device-master-port=%lu", opt_device_master);

      if (opt)
	{
	  err = argz_add (argz, argz_len, opt);
	  free (opt);
	}
    }

  return err;
}

static struct argp argp =
{ options, parse_opt, 0, "Hurd standard exec server." };

/* Setting this variable makes libtrivfs use our argp to
   parse options passed in an fsys_set_options RPC.  */
struct argp *trivfs_runtime_argp = &argp;

/* Get our stderr set up to print on the console, in case we have to
   panic or something.  */
error_t
open_console (mach_port_t device_master)
{
  static int got_console = 0;
  mach_port_t cons;
  error_t err;

  if (got_console)
    return 0;

  err = device_open (device_master, D_READ|D_WRITE, "console", &cons);
  if (err)
    return err;

  stdin = mach_open_devstream (cons, "r");
  stdout = stderr = mach_open_devstream (cons, "w");

  got_console = 1;
  mach_port_deallocate (mach_task_self (), cons);
  return 0;
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;

  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (MACH_PORT_VALID (opt_device_master))
    {
      err = open_console (opt_device_master);
      assert_perror_backtrace (err);
      mach_port_deallocate (mach_task_self (), opt_device_master);
    }

  save_argv = argv;

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (2, 0, "Must be started as a translator");

  /* Fetch our proc server port for easy use.  If we are booting, it is not
     set yet and `getproc' returns MACH_PORT_NULL; we reset PROCSERVER in
     S_exec_init (below).  */
  procserver = getproc ();

  err = trivfs_add_port_bucket (&port_bucket);
  if (err)
    error (1, 0, "error creating port bucket");

  err = trivfs_add_control_port_class (&trivfs_control_class);
  if (err)
    error (1, 0, "error creating control port class");

  err = trivfs_add_protid_port_class (&trivfs_protid_class);
  if (err)
    error (1, 0, "error creating protid port class");

  execboot_portclass = ports_create_class (deadboot, NULL);

  /* Reply to our parent.  */
  err = trivfs_startup (bootstrap, 0,
			trivfs_control_class, port_bucket,
			trivfs_protid_class, port_bucket,
			&fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "Contacting parent");

  /* Launch.  */
  ports_manage_port_operations_multithread (port_bucket, exec_demuxer,
					    2 * 60 * 1000, 0, 0);

  return 0;
}


void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_fstype = FSTYPE_MISC;
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  int count;

  /* Stop new requests.  */
  ports_inhibit_class_rpcs (trivfs_control_class);
  ports_inhibit_class_rpcs (trivfs_protid_class);

  /* Are there any extant user ports for the /servers/exec file?  */
  count = ports_count_class (trivfs_protid_class);
  if (count == 0 || (flags & FSYS_GOAWAY_FORCE))
    {
      /* No users.  Disconnect from the filesystem.  */
      mach_port_deallocate (mach_task_self (), fsys->underlying);

      /* Are there remaining exec_startup RPCs to answer?  */
      count = ports_count_class (execboot_portclass);
      if (count == 0)
	/* Nope.  We got no reason to live.  */
	exit (0);

      /* Continue servicing tasks starting up.  */
      ports_enable_class (execboot_portclass);

      /* No more communication with the parent filesystem.  */
      ports_destroy_right (fsys);

      return 0;
    }
  else
    {
      /* We won't go away, so start things going again...  */
      ports_enable_class (trivfs_protid_class);
      ports_resume_class_rpcs (trivfs_control_class);
      ports_resume_class_rpcs (trivfs_protid_class);

      return EBUSY;
    }
}

/* Sent by the bootstrap filesystem after the other essential
   servers have been started up.  */

kern_return_t
S_exec_init (struct trivfs_protid *protid,
	     auth_t auth, process_t proc)
{
  mach_port_t host_priv, device_master, startup;
  error_t err;

  if (! protid || ! protid->isroot)
    return EPERM;

  _hurd_port_set (&_hurd_ports[INIT_PORT_PROC], proc); /* Consume.  */
  _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], auth); /* Consume.  */

  /* Do initial setup with the proc server.  */
  _hurd_proc_init (save_argv, NULL, 0);

  procserver = getproc ();

  /* Have the proc server notify us when the canonical ints and ports
     change.  This will generate an immediate callback giving us the
     initial boot-time canonical sets.  */
  {
    struct iouser *user;
    struct trivfs_protid *cred;
    mach_port_t right;

    err = iohelp_create_empty_iouser (&user);
    assert_perror_backtrace (err);
    err = trivfs_open (fsys, user, 0, MACH_PORT_NULL, &cred);
    assert_perror_backtrace (err);

    right = ports_get_send_right (cred);
    proc_execdata_notify (procserver, right, MACH_MSG_TYPE_COPY_SEND);
    mach_port_deallocate (mach_task_self (), right);
  }

  err = get_privileged_ports (&host_priv, &device_master);
  assert_perror_backtrace (err);

  err = open_console (device_master);
  assert_perror_backtrace (err);
  mach_port_deallocate (mach_task_self (), device_master);

  proc_register_version (procserver, host_priv, "exec", "", HURD_VERSION);

  startup = file_name_lookup (_SERVERS_STARTUP, 0, 0);
  if (startup == MACH_PORT_NULL)
    {
      error (0, errno, "%s", _SERVERS_STARTUP);

      /* Fall back to abusing the message port lookup.  */
      err = proc_getmsgport (procserver, HURD_PID_STARTUP, &startup);
      assert_perror_backtrace (err);
    }
  mach_port_deallocate (mach_task_self (), procserver);

  /* Call startup_essential task last; init assumes we are ready to
     run once we call it. */
  err = startup_essential_task (startup, mach_task_self (), MACH_PORT_NULL,
				"exec", host_priv);
  assert_perror_backtrace (err);
  mach_port_deallocate (mach_task_self (), startup);

  mach_port_deallocate (mach_task_self (), host_priv);

  return 0;
}
