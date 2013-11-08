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
#include <assert.h>

void
ports_port_deref (void *portstruct)
{
  struct port_info *pi = portstruct;
  int trieddroppingweakrefs = 0;

 retry:
  
  pthread_mutex_lock (&_ports_lock);
  
  if (pi->refcnt == 1 && pi->weakrefcnt
      && pi->class->dropweak_routine && !trieddroppingweakrefs)
    {
      pthread_mutex_unlock (&_ports_lock);
      (*pi->class->dropweak_routine) (pi);
      trieddroppingweakrefs = 1;
      goto retry;
    }
  
  assert (pi->refcnt);

  pi->refcnt--;
  if (pi->refcnt == 0 && pi->weakrefcnt == 0)
    _ports_complete_deallocate (pi);
  else
    pthread_mutex_unlock (&_ports_lock);
}
