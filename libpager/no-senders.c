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
#include "notify_S.h"

error_t
_pager_do_mach_notify_no_senders (struct port_info *pi,
					 mach_port_mscount_t mscount)
{
  if (!pi ||
      pi->class != _pager_class)
    return EOPNOTSUPP;

  ports_no_senders (pi, mscount);

  return 0;
}
