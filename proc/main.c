/* Initialization of the proc server
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1999 Free Software Foundation

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
#include <hurd/startup.h>
#include <assert.h>
#include <wire.h>
#include <argp.h>
#include <version.h>

#include "proc.h"

const char *argp_program_version = STANDARD_HURD_VERSION (proc);

int
message_demuxer (mach_msg_header_t *inp,
		 mach_msg_header_t *outp)
{
  extern int process_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int notify_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int proc_exc_server (mach_msg_header_t *, mach_msg_header_t *);
  int status;

  mutex_lock (&global_lock);
  status = (process_server (inp, outp)
	    || notify_server (inp, outp)
	    || ports_interrupt_server (inp, outp)
	    || proc_exc_server (inp, outp));
  mutex_unlock (&global_lock);
  return status;
}

struct mutex global_lock = MUTEX_INITIALIZER;

int
main (int argc, char **argv, char **envp)
{
  mach_port_t boot;
  error_t err;
  mach_port_t pset, psetcntl;
  void *genport;
  process_t startup_port;
  struct argp argp = { 0, 0, 0, "Hurd process server" };

  argp_parse (&argp, argc, argv, 0, 0, 0);

  initialize_version_info ();

  err = task_get_bootstrap_port (mach_task_self (), &boot);
  assert_perror (err);
  if (boot == MACH_PORT_NULL)
    error (2, 0, "proc server can only be run by init during boot");

  proc_bucket = ports_create_bucket ();
  proc_class = ports_create_class (0, 0);
  generic_port_class = ports_create_class (0, 0);
  exc_class = ports_create_class (exc_clean, 0);
  ports_create_port (generic_port_class, proc_bucket,
		     sizeof (struct port_info), &genport);
  generic_port = ports_get_right (genport);

  /* new_proc depends on these assignments which must occur in this order. */
  self_proc = new_proc (mach_task_self ()); /* proc 0 is the procserver */
  startup_proc = new_proc (MACH_PORT_NULL); /* proc 1 is init */

  startup_port = ports_get_right (startup_proc);
  mach_port_insert_right (mach_task_self (), startup_port,
			  startup_port, MACH_MSG_TYPE_MAKE_SEND);
  err = startup_procinit (boot, startup_port, &startup_proc->p_task,
			  &authserver, &master_host_port, &master_device_port);
  assert_perror (err);
  mach_port_deallocate (mach_task_self (), startup_port);

  mach_port_mod_refs (mach_task_self (), authserver, MACH_PORT_RIGHT_SEND, 1);
  _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], authserver);
  mach_port_deallocate (mach_task_self (), boot);

  add_proc_to_hash (self_proc);
  add_proc_to_hash (startup_proc);

  /* Set our own argv and envp locations.  */
  self_proc->p_argv = (int) argv;
  self_proc->p_envp = (int) envp;

  /* Give ourselves good scheduling performance, because we are so
     important. */
  err = thread_get_assignment (mach_thread_self (), &pset);
  assert_perror (err);
  err = host_processor_set_priv (master_host_port, pset, &psetcntl);
  assert_perror (err);
  thread_max_priority (mach_thread_self (), psetcntl, 0);
  assert_perror (err);
  err = task_priority (mach_task_self (), 2, 1);
  assert_perror (err);

  mach_port_deallocate (mach_task_self (), pset);
  mach_port_deallocate (mach_task_self (), psetcntl);

/*  wire_task_self (); */

  while (1)
    ports_manage_port_operations_multithread (proc_bucket,
					      message_demuxer,
					      0, 0, 0);
}
