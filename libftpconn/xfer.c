/* Start/stop data channel transfer

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

#include <ftpconn.h>
#include "priv.h"

/* Open a data connection, returning the file descriptor in DATA.  */
static error_t
ftp_conn_open_data (struct ftp_conn *conn, int *data)
{
  struct sockaddr *addr;
  error_t err = ftp_conn_get_pasv_addr (conn, &addr);

  if (! err)
    {
      int dsock = socket (PF_INET, SOCK_STREAM, 0);

      if (dsock < 0)
	err = errno;
      else if (connect (dsock, addr, addr->sa_len) < 0)
	{
	  err = errno;
	  close (dsock);
	}
      else
	*data = dsock;

      free (addr);
    }

  return err;
}

/* Start a transfer command CMD/ARG, returning a file descriptor in DATA.
   POSS_ERRS is a list of errnos to try matching against any resulting error
   text.  */
error_t
ftp_conn_start_transfer (struct ftp_conn *conn,
			 const char *cmd, const char *arg,
			 const error_t *poss_errs,
			 int *data)
{
  error_t err = ftp_conn_open_data (conn, data);

  if (! err)
    {
      int reply;
      const char *txt;

      err = ftp_conn_cmd (conn, cmd, arg, &reply, &txt);
      if (!err && !REPLY_IS_PRELIM (reply))
	  err = unexpected_reply (conn, reply, txt, poss_errs);

      if (err)
	close (*data);
    }

  return err;
}

/* Wait for the reply signalling the end of a data transfer.  */
error_t
ftp_conn_finish_transfer (struct ftp_conn *conn)
{
  int reply;
  error_t err = ftp_conn_get_reply (conn, &reply, 0);
  if (!err && reply != REPLY_TRANS_OK && reply != REPLY_FCMD_OK)
    err = unexpected_reply (conn, reply, 0, 0);
  return err;
}

/* Start retreiving file NAME over CONN, returning a file descriptor in DATA
   over which the data can be read.  */
error_t
ftp_conn_start_retrieve (struct ftp_conn *conn, const char *name, int *data)
{
  if (! name)
    return EINVAL;
  return
    ftp_conn_start_transfer (conn, "retr", name, ftp_conn_poss_file_errs, data);
}

/* Start retreiving a list of files in NAME over CONN, returning a file
   descriptor in DATA over which the data can be read.  */
error_t
ftp_conn_start_list (struct ftp_conn *conn, const char *name, int *data)
{
  return
    ftp_conn_start_transfer (conn, "nlst", name, ftp_conn_poss_file_errs, data);
}

/* Start retreiving a directory listing of NAME over CONN, returning a file
   descriptor in DATA over which the data can be read.  */
error_t
ftp_conn_start_dir (struct ftp_conn *conn, const char *name, int *data)
{
  return
    ftp_conn_start_transfer (conn, "list", name, ftp_conn_poss_file_errs, data);
}

/* Start storing into file NAME over CONN, returning a file descriptor in DATA
   into which the data can be written.  */
error_t
ftp_conn_start_store (struct ftp_conn *conn, const char *name, int *data)
{
  if (! name)
    return EINVAL;
  return
    ftp_conn_start_transfer (conn, "stor", name, ftp_conn_poss_file_errs, data);
}
