/* By-hand stubs for some RPC calls
   Copyright (C) 1994,96,99,2000 Free Software Foundation, Inc.

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

#include <stdlib.h>
#include <hurd/hurd_types.h>
#include <mach.h>
#include <string.h>
#include <assert.h>

/* From hurd/msg.defs: */
#define RPCID_SIG_POST 23000


/* Send signal SIGNO to MSGPORT with REFPORT as reference.  Don't
   block in any fashion.  */
error_t
send_signal (mach_port_t msgport,
	     int signal,
	     mach_port_t refport,
	     mach_msg_timeout_t timeout)
{
  error_t err;

  /* This message buffer might be modified by mach_msg in some error cases,
     so we cannot safely reuse a static buffer.  */
  struct
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
      msgport,			/* msgh_remote_port */
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
    signal,
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
    refport
  };

  err = mach_msg (&message.head,
		  MACH_SEND_MSG|MACH_SEND_TIMEOUT, sizeof message, 0,
		  MACH_PORT_NULL, timeout, MACH_PORT_NULL);

  switch (err)
    {
    case MACH_SEND_TIMED_OUT:
      /* The send could not complete in time.  In this error case, the
	 kernel has modified the message buffer in a pseudo-receive
	 operation.  That means our COPY_SEND refs might now be MOVE_SEND
	 refs, in which case each has gained user ref accordingly.  To
	 avoid leaking those refs, we must clean up the buffer.  We don't
	 use mach_msg_destroy because it assumes the local/remote ports in
	 the header have been reversed as from a real receive, while a
	 pseudo-receive leaves them as they were.  */
      if (MACH_MSGH_BITS_REMOTE (message.head.msgh_bits)
	  == MACH_MSG_TYPE_MOVE_SEND)
	mach_port_deallocate (mach_task_self (),
			      message.head.msgh_remote_port);
      if (message.refporttype.msgt_name == MACH_MSG_TYPE_MOVE_SEND)
	mach_port_deallocate (mach_task_self (), message.refport);
      break;

      /* These are the other codes that mean a pseudo-receive modified
	 the message buffer and we might need to clean up the send rights.
	 None of them should be possible in our usage.  */
    case MACH_SEND_INTERRUPTED:
    case MACH_SEND_INVALID_NOTIFY:
    case MACH_SEND_NO_NOTIFY:
    case MACH_SEND_NOTIFY_IN_PROGRESS:
      assert_perror (err);
      break;

    default:			/* Other errors are safe to ignore.  */
      break;
    }

  return err;
}
