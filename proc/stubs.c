/* By-hand stubs for some RPC calls
   Copyright (C) 1994 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* Standard MiG goop */
#define EXPORT_BOOLEAN
#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/message.h>
#include <mach/notify.h>
#include <mach/mach_types.h>
#include <mach/mig_errors.h>
#include <mach/mig_support.h>
#include <mach/msg_type.h>
#include <mach/std_types.h>
#define msgh_request_port	msgh_remote_port
#define msgh_reply_port		msgh_local_port


#include "proc.h"
#include <cthreads.h>
#include <stdlib.h>
#include <hurd/hurd_types.h>


#ifndef __i386__
#error This code is i386 dependent
#else

struct msg_spec
{
  int len;
  void *contents;
};

/* Send the Mach message indicated by msg_spec; call cthread_exit
   when it has been delivered. */
static void
blocking_message_send (struct msg_spec *message)
{
  cthread_wire ();
  mach_msg ((mach_msg_header_t *)message->contents, MACH_SEND_MSG, 
	    message->len, 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, 
	    MACH_PORT_NULL);
  cthread_exit (0);
}

/* Send signal SIGNO to MSGPORT with REFPORT as reference.  Don't
   block in any fashion.  */
void
send_signal (mach_port_t msgport,
	     int signal,
	     mach_port_t refport)
{
  error_t err;

  /* Copied from MiG generated source for msgUser.c */
  typedef struct {
    mach_msg_header_t Head;
    mach_msg_type_t signalType;
    int signal;
    mach_msg_type_t refportType;
    mach_port_t refport;
  } Request;
  
  union {
    Request In;
  } Mess;
  
  register Request *InP = &Mess.In;
  
  
  static const mach_msg_type_t signalType = {
    /* msgt_name = */		2,
				/* msgt_size = */		32,
				/* msgt_number = */		1,
				/* msgt_inline = */		TRUE,
				/* msgt_longform = */		FALSE,
				/* msgt_deallocate = */		FALSE,
				/* msgt_unused = */		0
  };
  
  static const mach_msg_type_t refportType = {
    /* msgt_name = */		19,
				/* msgt_size = */		32,
				/* msgt_number = */		1,
				/* msgt_inline = */		TRUE,
				/* msgt_longform = */		FALSE,
				/* msgt_deallocate = */		FALSE,
				/* msgt_unused = */		0
  };
  
  InP->signalType = signalType;
  
  InP->signal = signal;
  
  InP->refportType = refportType;
  
  InP->refport = refport;
  
  InP->Head.msgh_bits = MACH_MSGH_BITS_COMPLEX|
    MACH_MSGH_BITS(19, 21);
  /* msgh_size passed as argument */
  InP->Head.msgh_request_port = msgport;
  InP->Head.msgh_reply_port = MACH_PORT_NULL;
  InP->Head.msgh_seqno = 0;
  InP->Head.msgh_id = 23000;
  
  /* From here on down is new code (and the point of this exercise. */

  err = mach_msg(&InP->Head, MACH_SEND_MSG|MACH_SEND_TIMEOUT, 40, 0, 
		 MACH_PORT_NULL, 0, MACH_PORT_NULL);

  if (err == MACH_SEND_TIMEOUT)
    {
      struct msg_spec *msg_spec = malloc (sizeof (struct msg_spec));
      
      msg_spec->len = 40;
      msg_spec->contents = malloc (40);
      bcopy (&InP->Head, msg_spec->contents, 40);
      
      cthread_detach (cthread_fork ((cthread_fn_t) blocking_message_send,
				    msg_spec));
    }
}

  
#endif /* __i386__ */
