/* Create a new ftp connection

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

/* Create a new ftp connection as specified by PARAMS, and return it in CONN;
   HOOKS contains customization hooks used by the connection.  Neither PARAMS
   nor HOOKS is copied, so a copy of it should be made if necessary before
   calling this function; if it should be freed later, a FINI hook may be
   used to do so.  */
error_t
ftp_conn_create (const struct ftp_conn_params *params,
		 const struct ftp_conn_hooks *hooks,
		 struct ftp_conn **conn)
{
  error_t err;
  struct ftp_conn *new = malloc (sizeof (struct ftp_conn));

  if (! new)
    return ENOMEM;

  new->control = -1;
  new->line = 0;
  new->line_sz = 0;
  new->line_offs = 0;
  new->line_len = 0;
  new->reply_txt = 0;
  new->reply_txt_sz = 0;
  new->params = params;
  new->hooks = hooks;
  new->syshooks_valid = 0;
  new->use_passive = 1;
  new->actv_data_addr = 0;
  new->cwd = 0;
  new->type = 0;
  memset (&new->syshooks, 0, sizeof new->syshooks);

  if (new->hooks && new->hooks->init)
    err = (*new->hooks->init) (new);
  else
    err = 0;

  if (err)
    ftp_conn_free (new);
  else
    *conn = new;

  return err;
}

/* Free the ftp connection CONN, closing it first, and freeing all resources
   it uses.  */
void
ftp_conn_free (struct ftp_conn *conn)
{
  ftp_conn_close (conn);
  if (conn->hooks && conn->hooks->fini)
    (* conn->hooks->fini) (conn);
  if (conn->line)
    free (conn->line);
  if (conn->reply_txt)
    free (conn->reply_txt);
  if (conn->actv_data_addr)
    free (conn->actv_data_addr);
  free (conn);
}
