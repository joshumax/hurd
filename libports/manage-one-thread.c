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
		    mach_msg_header_t *outp)
    {
      struct port_info *pi;
      struct rpc_info link;
      int status;
      error_t err;

      pi = ports_lookup_port (bucket, inp->msgh_local_port, 0);
      if (pi)
	{
	  err = ports_begin_rpc (pi, inp->msgh_id, &link);
	  if (err)
	    {
	      mach_port_deallocate (mach_task_self (), inp->msgh_remote_port);
	      status = 0;
	    }
	  else
	    {
	      /* No need to check cancel threshhold here, because
		 in a single threaded server the cancel is always
		 handled in order. */
	      status = demuxer (inp, outp);
	      ports_end_rpc (pi, &link);
	    }
	  ports_port_deref (pi);
	}
      else
	status = 0;

      return status;
    }
  
  do
    err = mach_msg_server_timeout (internal_demuxer, 0, bucket->portset, 
				   timeout ? MACH_RCV_TIMEOUT : 0, timeout);
  while (err != MACH_RCV_TIMED_OUT);
}
