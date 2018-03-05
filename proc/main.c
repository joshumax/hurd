/* Initialization of the proc server
   Copyright (C) 1993,94,95,96,97,99,2000,01,13 Free Software Foundation, Inc.

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

#include <mach.h>
#include <hurd/hurd_types.h>
#include <hurd.h>
#include <hurd/paths.h>
#include <hurd/startup.h>
#include <device/device.h>
#include <assert-backtrace.h>
#include <argp.h>
#include <error.h>
#include <version.h>
#include <pids.h>

#include "proc.h"
#include "gnumach_U.h"

const char *argp_program_version = STANDARD_HURD_VERSION (proc);

#include "process_S.h"
#include "notify_S.h"
#include "../libports/interrupt_S.h"
#include "proc_exc_S.h"
#include "task_notify_S.h"

int
message_demuxer (mach_msg_header_t *inp,
		 mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = process_server_routine (inp)) ||
      (routine = notify_server_routine (inp)) ||
      (routine = ports_interrupt_server_routine (inp)) ||
      (routine = proc_exc_server_routine (inp)) ||
      (routine = task_notify_server_routine (inp)))
    {
      pthread_mutex_lock (&global_lock);
      (*routine) (inp, outp);
      pthread_mutex_unlock (&global_lock);
      return TRUE;
    }
  else
    return FALSE;
}

pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;
int startup_fallback;

error_t
increase_priority (void)
{
  mach_port_t pset = MACH_PORT_NULL, psetcntl = MACH_PORT_NULL;
  error_t err;

  err = thread_get_assignment (mach_thread_self (), &pset);
  if (err)
    goto out;

  err = host_processor_set_priv (_hurd_host_priv, pset, &psetcntl);
  if (err)
    goto out;

  err = thread_max_priority (mach_thread_self (), psetcntl, 0);
  /* If we are running in an unprivileged subhurd, we got a faked
     privileged processor set port.  This is indeed a kind of
     permission problem, and we treat it as such.  */
  if (err == KERN_INVALID_ARGUMENT)
    err = EPERM;
  if (err)
    goto out;

  err = task_priority (mach_task_self (), 2, 1);

 out:
  if (MACH_PORT_VALID (pset))
    mach_port_deallocate (mach_task_self (), pset);
  if (MACH_PORT_VALID (psetcntl))
    mach_port_deallocate (mach_task_self (), psetcntl);

  return err;
}

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



static task_t kernel_task;

#define OPT_KERNEL_TASK	-1

static struct argp_option
options[] =
{
  {"kernel-task", OPT_KERNEL_TASK, "PORT"},
  {0}
};

static int
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
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
  mach_port_t boot;
  error_t err;
  void *genport;
  process_t startup_port;
  mach_port_t startup;
  struct argp argp = { options, parse_opt, 0, "Hurd process server" };

  argp_parse (&argp, argc, argv, 0, 0, 0);

  initialize_version_info ();

  err = task_get_bootstrap_port (mach_task_self (), &boot);
  assert_perror_backtrace (err);
  if (boot == MACH_PORT_NULL)
    error (2, 0, "proc server can only be run by startup during boot");

  proc_bucket = ports_create_bucket ();
  proc_class = ports_create_class (0, 0);
  generic_port_class = ports_create_class (0, 0);
  exc_class = ports_create_class (exc_clean, 0);
  ports_create_port (generic_port_class, proc_bucket,
		     sizeof (struct port_info), &genport);
  generic_port = ports_get_right (genport);

  /* Create the initial proc object for init (PID 1).  */
  init_proc = create_init_proc ();

  /* Create the startup proc object for /hurd/startup (PID 2).  */
  startup_proc = allocate_proc (MACH_PORT_NULL);
  startup_proc->p_deadmsg = 1;
  complete_proc (startup_proc, HURD_PID_STARTUP);

  /* Create our own proc object.  */
  self_proc = allocate_proc (mach_task_self ());
  assert_backtrace (self_proc);

  complete_proc (self_proc, HURD_PID_PROC);

  startup_port = ports_get_send_right (startup_proc);
  err = startup_procinit (boot, startup_port, &startup_proc->p_task,
			  &authserver, &_hurd_host_priv, &_hurd_device_master);
  assert_perror_backtrace (err);
  mach_port_deallocate (mach_task_self (), startup_port);

  /* Get our stderr set up to print on the console, in case we have
     to panic or something.  */
  open_console (_hurd_device_master);

  mach_port_mod_refs (mach_task_self (), authserver, MACH_PORT_RIGHT_SEND, 1);
  _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], authserver);
  mach_port_deallocate (mach_task_self (), boot);

  proc_death_notify (startup_proc);
  add_proc_to_hash (startup_proc); /* Now that we have the task port.  */

  /* Set our own argv and envp locations.  */
  self_proc->p_argv = (vm_address_t) argv;
  self_proc->p_envp = (vm_address_t) envp;

  /* Give ourselves good scheduling performance, because we are so
     important. */
  err = increase_priority ();
  if (err && err != EPERM)
    error (0, err, "Increasing priority failed");

  /* Find the kernel.  */
  if (MACH_PORT_VALID (kernel_task))
    kernel_proc = task_find (kernel_task);
  else
    {
      /* Get a list of all tasks to find the kernel.  */
      add_tasks (MACH_PORT_NULL);
      kernel_proc = pid_find (HURD_PID_KERNEL);
    }

  /* Register for new task notifications using the kernel's process as
     the port.  */
  err = register_new_task_notification (_hurd_host_priv,
					kernel_proc
                                        ? ports_get_right (kernel_proc)
                                        : generic_port,
					MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    error (0, err, "Registering task notifications failed");

  startup = file_name_lookup (_SERVERS_STARTUP, 0, 0);
  if (MACH_PORT_VALID (startup))
    {
      err = startup_essential_task (startup, mach_task_self (),
				    MACH_PORT_NULL, "proc", _hurd_host_priv);
      if (err)
	/* Due to the single-threaded nature of /hurd/startup, it can
	   only handle requests once the core server bootstrap has
	   completed.  Therefore, it does not bind itself to
	   /servers/startup until it is ready.	*/
	/* Fall back to abusing the message port lookup.  */
	startup_fallback = 1;

      err = mach_port_deallocate (mach_task_self (), startup);
      assert_perror_backtrace (err);
    }
  else
    /* Fall back to abusing the message port lookup.	*/
    startup_fallback = 1;

  while (1)
    ports_manage_port_operations_multithread (proc_bucket,
					      message_demuxer,
					      0, 0, 0);
}
