/* Initialization of the proc server
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

#include <mach.h>
#include <hurd/hurd_types.h>
#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/startup.h>

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

void
main ()
{
  mach_port_t boot;
  mach_port_t authhandle;

  initprimes ();

  task_get_bootstrap_port (mach_task_self (), &boot);

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET,
		      &request_portset);

  self_proc = new_proc (mach_task_self ()); /* proc 0 is the procserver */
  startup_proc = new_proc (MACH_PORT_NULL); /* proc 1 is init */

  startup_procinit (boot, startup_proc->p_reqport, &startup_proc->p_task,
		    &authhandle, &master_host_port, &master_device_port);

  _hurd_port_set (&_hurd_ports[INIT_PORT_AUTH], authhandle);
  mach_port_deallocate (mach_task_self (), boot);

  add_proc_to_hash (self_proc);
  add_proc_to_hash (startup_proc);
  
  while (1)
    mach_msg_server (message_demuxer, 0, request_portset);
}
