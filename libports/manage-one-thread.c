/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
   Writtenb by Michael I. Bushnell.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "ports.h"

void
ports_manage_port_operations_one_thread (struct port_bucket *bucket,
					 ports_demuxer_type demuxer,
					 int timeout)
{
  error_t err;

  int 
  internal_demuxer (mach_msg_header_t *inp,
		    mach_msg_header_t *outheadp)
    {
      struct port_info *pi;
      struct rpc_info link;
      int status;
      error_t err;
      register mig_reply_header_t *outp = (mig_reply_header_t *) outheadp;
      static const mach_msg_type_t RetCodeType = {
		/* msgt_name = */		MACH_MSG_TYPE_INTEGER_32,
		/* msgt_size = */		32,
		/* msgt_number = */		1,
		/* msgt_inline = */		TRUE,
		/* msgt_longform = */		FALSE,
		/* msgt_deallocate = */		FALSE,
		/* msgt_unused = */		0
	};

      /* Fill in default response. */
      outp->Head.msgh_bits 
	= MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(inp->msgh_bits), 0);
      outp->Head.msgh_size = sizeof *outp;
      outp->Head.msgh_remote_port = inp->msgh_remote_port;
      outp->Head.msgh_local_port = MACH_PORT_NULL;
      outp->Head.msgh_seqno = 0;
      outp->Head.msgh_id = inp->msgh_id + 100;
      outp->RetCodeType = RetCodeType;
      outp->RetCode = MIG_BAD_ID;

      pi = ports_lookup_port (bucket, inp->msgh_local_port, 0);
      if (pi)
	{
	  err = ports_begin_rpc (pi, inp->msgh_id, &link);
	  if (err)
	    {
	      mach_port_deallocate (mach_task_self (), inp->msgh_remote_port);
	      outp->RetCode = err;
	      status = 1;
	    }
	  else
	    {
	      /* No need to check cancel threshold here, because
		 in a single threaded server the cancel is always
		 handled in order. */
	      status = demuxer (inp, outheadp);
	      ports_end_rpc (pi, &link);
	    }
	  ports_port_deref (pi);
	}
      else
	{
	  outp->RetCode = EOPNOTSUPP;
	  status = 1;
	}

      return status;
    }
  
  do
    err = mach_msg_server_timeout (internal_demuxer, 0, bucket->portset, 
				   timeout ? MACH_RCV_TIMEOUT : 0, timeout);
  while (err != MACH_RCV_TIMED_OUT);
}
