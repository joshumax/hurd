/* 
   Copyright (C) 1994, 1995 Free Software Foundation

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

#include "priv.h"
#include <hurd/pager.h>

/* Called by the kernel when a port has no more senders.  We arrange
   to have this sent to the port which is out of senders (NOTIFY).  MSCOUNT
   is the make-send count of the port when the notification was generated;
   SEQNO is the sequence number of the message dequeue.  */
error_t
diskfs_do_seqnos_mach_notify_no_senders (mach_port_t notify,
					 mach_port_seqno_t seqno __attribute__ ((unused)),
					 mach_port_mscount_t mscount)
{
  struct port_info *pt;

  pt = ports_lookup_port (diskfs_port_bucket, notify, 0);

  ports_no_senders (pt, mscount);

  ports_port_deref (pt);
  
  return 0;
}
