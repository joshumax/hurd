/* 
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

/* Called by the kernel when a port has no more senders.  We arrange
   to have this sent to the port which is out of senders (NOTIFY).  MSCOUNT
   is the make-send count of the port when the notification was generated;
   SEQNO is the sequence number of the message dequeue.  */
error_t
diskfs_do_seqnos_mach_notify_no_senders (mach_port_t notify,
					 mach_port_seqno_t seqno,
					 mach_port_mscount_t mscount)
{
  struct port_inf *pt;

  pt = ports_get_port (notify);

  if (pt->type == PT_PAGER)
    pager_no_senders ((struct pager *)upt, seqno, mscount);
  else
    {
      ports_done_with_port (pt);
      ports_done_with_port (pt);
    }
  
  return 0;
}
