/* Initialization of the proc server
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

#include <mach.h>
#include <hurd/hurd_types.h>
#include <hurd.h>
#include <hurd/startup.h>
#include <assert.h>

#include "proc.h"



int
message_demuxer (mach_msg_header_t *inp,
		 mach_msg_header_t *outp)
{
  extern int process_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int notify_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int interrupt_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int proc_exc_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int proc_excrepl_server (mach_msg_header_t *, mach_msg_header_t *);

  return (process_server (inp, outp)
	  || notify_server (inp, outp)
	  || interrupt_server (inp, outp)
	  || proc_exc_server (inp, outp)
	  || proc_excrepl_server (inp, outp));
}

int
main (int argc, char **argv, char **envp)
{
  mach_port_t boot;
  error_t err;
  mach_port_t pset, psetcntl;

  initialize_version_info ();

  task_get_bootstrap_port (mach_task_self (), &boot);

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET,
		      &request_portset);

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &generic_port);
  mach_port_move_member (mach_task_self (), generic_port, request_portset);

  /* new_proc depends on these assignments which must occur in this order. */
  self_proc = new_proc (mach_task_self ()); /* proc 0 is the procserver */
  startup_proc = new_proc (MACH_PORT_NULL); /* proc 1 is init */

  mach_port_insert_right (mach_task_self (), startup_proc->p_reqport,
			  startup_proc->p_reqport, MACH_MSG_TYPE_MAKE_SEND);
  err = startup_procinit (boot, startup_proc->p_reqport, &startup_proc->p_task,
			  &authserver, &master_host_port, &master_device_port);
  assert (!err);
  mach_port_deallocate (mach_task_self (), startup_proc->p_reqport);

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
  assert (!err);
  err = host_processor_set_priv (master_host_port, pset, &psetcntl);
  assert (!err);
  thread_max_priority (mach_thread_self (), psetcntl, 0);
  assert (!err);
  err = task_priority (mach_task_self (), 2, 1);
  assert (!err);

  mach_port_deallocate (mach_task_self (), pset);
  mach_port_deallocate (mach_task_self (), psetcntl);

  {
    extern void _start ();
    extern char _edata, _etext, __data_start;
    vm_address_t text_start = (vm_address_t) &_start;
    err = vm_wire (master_host_port, mach_task_self (), 
		   (vm_address_t) text_start,
		   (vm_size_t) (&_etext - text_start),
		   VM_PROT_READ|VM_PROT_EXECUTE);
    err = vm_wire (master_host_port, mach_task_self (), 
		   (vm_address_t) &__data_start,
		   (vm_size_t) (&_edata - &__data_start), 
		   VM_PROT_READ|VM_PROT_WRITE);
  }

  while (1)
    mach_msg_server (message_demuxer, 0, request_portset);
}
