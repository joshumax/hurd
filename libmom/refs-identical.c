/* Tell if two mom port references refer to the same channel
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "priv.h"

int
mom_ports_identical (struct mom_port_ref *obj1, 
		     struct mom_port_ref *obj2)
{
  int ret;
  
 tryagain:
  spin_lock (&obj1->lock);
  if (!spin_try_lock (&obj2->lock))
    {
      spin_unlock (&obj1->lock);
      goto tryagain;
    }
  assert (obj1->refcnt);
  assert (obj2->refcnt);
  
  ret = (obj1->port == obj2->port);
  spin_unlock (&obj1->lock);
  spin_unlock (&obj2->lock);
  return ret;
}
