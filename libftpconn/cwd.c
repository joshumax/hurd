/* Get/set connection current working directory

   Copyright (C) 1997, 1998 Free Software Foundation, Inc.

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

static error_t
_cache_cwd (struct ftp_conn *conn, int reopen)
{
  int reply;
  const char *txt;
  error_t err =
    (reopen ? ftp_conn_cmd_reopen : ftp_conn_cmd) (conn, "pwd", 0, &reply, &txt);

  if (! err)
    {
      if (reply == REPLY_DIR_NAME)
	{
	  char *cwd = malloc (strlen (txt));
	  if (! cwd)
	    err = ENOMEM;
	  else if (sscanf (txt, "\"%[^\"]\"", cwd) != 1)
	    err = EGRATUITOUS;
	  else
	    {
	      if (conn->cwd)
		free (conn->cwd);
	      conn->cwd = cwd;
	    }
	}
      else
	err = unexpected_reply (conn, reply, txt, 0);
    }

  return err;
}

/* Return a malloced string containing CONN's working directory in CWD.  */
error_t
ftp_conn_get_cwd (struct ftp_conn *conn, char **cwd)
{
  error_t err = 0;
  if (! conn->cwd)
    err = _cache_cwd (conn, 1);
  if (! err)
    {
      *cwd = strdup (conn->cwd);
      if (! *cwd)
	err = ENOMEM;
    }
  return err;
}

/* Return a malloced string containing CONN's working directory in CWD.  */
error_t
ftp_conn_cwd (struct ftp_conn *conn, const char *cwd)
{
  error_t err = 0;
  if (conn->cwd && strcmp (conn->cwd, cwd) == 0)
    err = 0;
  else
    {
      int reply;
      const char *txt;
      err = ftp_conn_cmd_reopen (conn, "cwd", cwd, &reply, &txt);
      if (! err)
	{
	  if (reply == REPLY_FCMD_OK)
	    err = _cache_cwd (conn, 0);
	  else
	    err = unexpected_reply (conn, reply, txt, ftp_conn_poss_file_errs);
	}
    }
  return err;
}

/* Return a malloced string containing CONN's working directory in CWD.  */
error_t
ftp_conn_cdup (struct ftp_conn *conn)
{
  int reply;
  const char *txt;
  error_t err = ftp_conn_cmd_reopen (conn, "cdup", 0, &reply, &txt);
  if (! err)
    {
      if (reply == REPLY_OK)
	err = _cache_cwd (conn, 0);
      else
	err = unexpected_reply (conn, reply, txt, ftp_conn_poss_file_errs);
    }
  return err;
}
