/* 
   Copyright (C) 1995, 2001 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
#include <assert-backtrace.h>

void
ports_port_deref (void *portstruct)
{
  struct port_info *pi = portstruct;
  struct references result;

  if (pi->class->dropweak_routine)
    {
      /* If we need to call the dropweak routine, we need to hold one
         reference while doing so.  We use a weak reference for this
         purpose, which we acquire by demoting our hard reference to a
         weak one.  */
      refcounts_demote (&pi->refcounts, &result);

      if (result.hard == 0 && result.weak > 1)
        (*pi->class->dropweak_routine) (pi);

      refcounts_deref_weak (&pi->refcounts, &result);
    }
  else
    refcounts_deref (&pi->refcounts, &result);

  if (result.hard == 0 && result.weak == 0)
    _ports_complete_deallocate (pi);
}
