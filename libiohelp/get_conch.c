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

/* The conch must be locked when calling this routine. */
/* Remove any current holder of conch C. */
void
iohelp_get_conch (struct conch *c)
{
  struct shared_io *user_sh;
  
 again:
  user_sh = c->holder_shared_page;
  
  if (user_sh)
    {
      pthread_spin_lock (&user_sh->lock);
      switch (user_sh->conch_status)
	{
	case USER_HAS_CONCH:
	  user_sh->conch_status = USER_RELEASE_CONCH;
	  /* fall through ... */
	case USER_RELEASE_CONCH:
	  pthread_spin_unlock (&user_sh->lock);
	  pthread_cond_wait (&c->wait, c->lock);
	  /* Anything can have happened */
	  goto again;
	  
	case USER_COULD_HAVE_CONCH:
	  user_sh->conch_status = USER_HAS_NOT_CONCH;
	  pthread_spin_unlock (&user_sh->lock);
	  iohelp_fetch_shared_data (c->holder);
	  break;
	  
	case USER_HAS_NOT_CONCH:
	  pthread_spin_unlock (&user_sh->lock);
	  break;
	}
    }
  c->holder = 0;
  c->holder_shared_page = 0;
}
