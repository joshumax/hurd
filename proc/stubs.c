/* By-hand stubs for some RPC calls
   Copyright (C) 1994, 1996, 1999 Free Software Foundation

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

#include <cthreads.h>
#include <stdlib.h>
#include <hurd/hurd_types.h>
#include <mach/message.h>
#include <string.h>

#include "proc.h"

/* From hurd/msg.defs: */
#define RPCID_SIG_POST 23000

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

  static struct
    {
      mach_msg_header_t head;
      mach_msg_type_t signaltype;
      int signal;
      mach_msg_type_t sigcode_type;
      natural_t sigcode;
      mach_msg_type_t refporttype;
      mach_port_t refport;
    }
  message = 
    {
      {
	/* Message header: */
	(MACH_MSGH_BITS_COMPLEX 
	 | MACH_MSGH_BITS (MACH_MSG_TYPE_COPY_SEND,
			  MACH_MSG_TYPE_MAKE_SEND_ONCE)), /* msgh_bits */
	sizeof message,		/* msgh_size */
	0,			/* msgh_remote_port */
	MACH_PORT_NULL,		/* msgh_local_port */
	0,			/* msgh_seqno */
	RPCID_SIG_POST,		/* msgh_id */
      },
      {
	/* Type descriptor for signo */
	MACH_MSG_TYPE_INTEGER_32, /* msgt_name */
	32,			/* msgt_size */
	1,			/* msgt_number */
	1,			/* msgt_inline */
	0,			/* msgt_longform */
	0,			/* msgt_deallocate */
	0,			/* msgt_unused */
      },
      /* Signal number */
      0,
      /* Type descriptor for sigcode */
      {
	MACH_MSG_TYPE_INTEGER_32, /* msgt_name */
	32,			/* msgt_size */
	1,			/* msgt_number */
	1,			/* msgt_inline */
	0,			/* msgt_longform */
	0,			/* msgt_deallocate */
	0,			/* msgt_unused */
      },
      /* Sigcode */
      0,
      {
	/* Type descriptor for refport */
	MACH_MSG_TYPE_COPY_SEND, /* msgt_name */
	32,			/* msgt_size */
	1,			/* msgt_number */
	1,			/* msgt_inline */
	0,			/* msgt_longform */
	0,			/* msgt_deallocate */
	0,			/* msgt_unused */
      },
      /* Reference port */
      MACH_PORT_NULL,
    };
  
  message.head.msgh_remote_port = msgport;
  message.signal = signal;
  message.refport = refport;

  err = mach_msg((mach_msg_header_t *)&message, 
		 MACH_SEND_MSG|MACH_SEND_TIMEOUT, sizeof message, 0,
		 MACH_PORT_NULL, 0, MACH_PORT_NULL);

  if (err == MACH_SEND_TIMEOUT)
    {
      struct msg_spec *msg_spec = malloc (sizeof (struct msg_spec));
      
      msg_spec->len = sizeof message;
      msg_spec->contents = malloc (sizeof message);
      bcopy (&message, msg_spec->contents, sizeof message);
      
      cthread_detach (cthread_fork ((cthread_fn_t) blocking_message_send,
				    msg_spec));
    }
}
