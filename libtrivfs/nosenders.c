/*
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

#include "priv.h"
#include "notify_S.h"

/* Called by the kernel when a port has no more senders.  We arrange
   to have this sent to the port which is out of senders (NOTIFY).  MSCOUNT
   is the make-send count of the port when the notification was generated. */
kern_return_t
trivfs_do_mach_notify_no_senders (mach_port_t notify,
				  mach_port_mscount_t mscount)
{
  struct port_info *pt;

  pt = ports_get_port (notify);
  if (!pt)
    return EOPNOTSUPP;

  ports_no_senders (pt, mscount);

  ports_done_with_port (pt);
  
  return 0;
}
