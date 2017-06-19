/* vcons-close.c - Close a virtual console.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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

#include <assert-backtrace.h>

#include <hurd.h>
#include <hurd/ports.h>
#include <pthread.h>

#include "cons.h"

/* Close the virtual console VCONS.  */
void
cons_vcons_close (vcons_t vcons)
{
  cons_t cons = vcons->cons;
  vcons_list_t vcons_entry = vcons->vcons_entry;

  pthread_mutex_lock (&cons->lock);
  /* The same virtual console should never be opened twice.  */
  assert_backtrace (vcons_entry->vcons == vcons);
  vcons_entry->vcons = NULL;
  pthread_mutex_unlock (&cons->lock);

  /* Destroy the port.  */
  ports_port_deref (vcons);
  ports_destroy_right (vcons);
}
