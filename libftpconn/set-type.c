/* Set connection type

   Copyright (C) 1997 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <ftpconn.h>
#include "priv.h"

/* Set the ftp connection type of CONN to TYPE, or return an error.  */
error_t
ftp_conn_set_type (struct ftp_conn *conn, const char *type)
{
  error_t err = 0;

  if (! type)
    return EINVAL;

  if (!conn->type || strcmp (type, conn->type) != 0)
    {
      type = strdup (type);
      if (! type)
	err = ENOMEM;
      else
	{
	  int reply;
	  error_t err = ftp_conn_cmd (conn, "type", type, &reply, 0);

	  if (!err && reply != REPLY_OK && reply != REPLY_CLOSED)
	    err = unexpected_reply (conn, reply, 0, 0);

	  if (!err || err == EPIPE)
	    {
	      if (conn->type)
		free ((char *)conn->type);
	      conn->type = type;
	    }
	}
    }    

  return err;
}
