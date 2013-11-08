/* 
   Copyright (C) 1993, 1994, 1996 Free Software Foundation

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

#include "iohelp.h"

/* Called by an I/O server to initialize a conch structure C; 
   M will be used to lock conch data structures.  */
void
iohelp_initialize_conch (struct conch *c, pthread_mutex_t *m)
{
  c->lock = m;
  pthread_cond_init (&c->wait, NULL);
  c->holder = 0;
  c->holder_shared_page = 0;
}
