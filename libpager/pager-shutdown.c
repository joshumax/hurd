/* Pager shutdown in pager library
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

/* Shutdown pager P and prevent any future paging activity on it.  */
void
pager_shutdown (struct pager *p)
{
  /* Sync and flush pager */
  pager_sync (p, 1);
  pager_flush (p, 1);
  pthread_mutex_lock (&p->interlock);
  p->pager_state = SHUTDOWN;
  ports_destroy_right (p);
  pthread_mutex_unlock (&p->interlock);
}
