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

/* Called by an I/O server upon receipt of an io_release_conch message;
   The user identified by USER is done with conch C; release it and 
   allow a waiting user to obtain the conch.  */
void
iohelp_handle_io_release_conch (struct conch *c, void *user)
{
  struct shared_io *user_sh = c->holder_shared_page;

  pthread_spin_lock (&user_sh->lock);
  if (c->holder_shared_page->conch_status != USER_HAS_NOT_CONCH)
    {
      c->holder_shared_page->conch_status = USER_HAS_NOT_CONCH;
      iohelp_fetch_shared_data (c->holder);
    }
  pthread_spin_unlock (&user_sh->lock);

  if (c->holder == user)
    {
      c->holder = 0;
      c->holder_shared_page = 0;
    }

  pthread_cond_broadcast (&c->wait);
}
