/* 
   Copyright (C) 1993, 1994 Free Software Foundation

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


#include "ioserver.h"

/* Called by an I/O server when an io_get_conch message is received.
   The user represented by USER and USER_SH wants conch C; give it
   to her or return an error.  */
error_t
ioserver_handle_io_get_conch (struct conch *c, void *user,
			      struct shared_io *user_sh)
{
  error_t error = 0;
  
  if (c->holder == user)
    {
      if (user_sh->conch_status != USER_HAS_NOT_CONCH)
	error = ioserver_fetch_shared_data (user);
      else
	user_sh->accessed = user_sh->written = 0;
      
      if (!error)
	error = ioserver_put_shared_data (user);
      if (!error)
	user_sh->conch_status = USER_HAS_CONCH;
    }
  else
    {
      error = ioserver_get_conch (c);

      if (!error)
	{
	  c->holder = user;
	  c->holder_shared_page = user_sh;
	  if (user_sh->conch_status == USER_HAS_NOT_CONCH)
	    user_sh->accessed = user_sh->written = 0;
	  user_sh->conch_status = USER_HAS_CONCH;
	  ioserver_put_shared_data (user);
	}
    }
  
  return error;
}
