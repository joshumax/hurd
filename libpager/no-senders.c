/* Called when a nosenders notification happens
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
#include <mach/notify.h>

error_t
_pager_do_seqnos_mach_notify_no_senders (mach_port_t notify,
					 mach_port_seqno_t seqno,
					 mach_port_mscount_t mscount)
{
  struct pager *p = ports_lookup_port (0, notify, _pager_class);
  
  if (!p)
    return EOPNOTSUPP;
  
  pthread_mutex_lock (&p->interlock);
  _pager_wait_for_seqno (p, seqno);
  _pager_release_seqno (p, seqno);
  pthread_mutex_unlock (&p->interlock);
  
  ports_no_senders (p, mscount);

  ports_port_deref (p);
  return 0;
}


