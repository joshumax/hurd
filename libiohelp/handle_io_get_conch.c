/* 
   Copyright (C) 19931996 Free Software Foundation

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

/* Called by an I/O server when an io_get_conch message is received.
   The user represented by USER and USER_SH wants conch C; give it
   to her or return an error.  */
void
iohelp_handle_io_get_conch (struct conch *c, void *user,
			      struct shared_io *user_sh)
{
  
  if (c->holder == user)
    {
      if (user_sh->conch_status != USER_HAS_NOT_CONCH)
	iohelp_fetch_shared_data (user);
      else
	user_sh->accessed = user_sh->written = 0;
      
      iohelp_put_shared_data (user);
      user_sh->conch_status = USER_HAS_CONCH;
    }
  else
    {
      iohelp_get_conch (c);

      c->holder = user;
      c->holder_shared_page = user_sh;
      if (user_sh->conch_status == USER_HAS_NOT_CONCH)
	user_sh->accessed = user_sh->written = 0;
      user_sh->conch_status = USER_HAS_CONCH;
      iohelp_put_shared_data (user);
    }
}
