/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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

#include "fshelp.h"
#include <pthread.h>

void
fshelp_transbox_init (struct transbox *transbox,
		      pthread_mutex_t *lock,
		      void *cookie)
{
  transbox->active = MACH_PORT_NULL;
  transbox->flags = 0;
  transbox->lock = lock;
  pthread_cond_init (&transbox->wakeup, NULL);
  transbox->cookie = cookie;
}
