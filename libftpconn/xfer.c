/* Start/stop data channel transfer

   Copyright (C) 1997,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
#include <netinet/in.h>

#include <ftpconn.h>
#include "priv.h"

/* Open an active data connection, returning the file descriptor in DATA.  */
static error_t
ftp_conn_start_open_actv_data (struct ftp_conn *conn, int *data)
{
  error_t err = 0;
  /* DCQ is a socket on which to listen for data connections from the server. */
  int dcq;
  struct sockaddr *addr = conn->actv_data_addr;
  socklen_t addr_len = sizeof *addr;

  if (! addr)
    /* Generate an address for the data connection (which we must know,
       so we can tell the server).  */
    {
      addr = conn->actv_data_addr = malloc (sizeof (struct sockaddr_in));
      if (! addr)
	return ENOMEM;

      /* Get the local address chosen by the system.  */
      if (conn->control < 0)
	err = EBADF;
      else if (getsockname (conn->control, addr, &addr_len) < 0)
	err = errno;

      if (err == EBADF || err == EPIPE)
	/* Control connection has closed; reopen it and try again.  */
	{
	  err = ftp_conn_open (conn);
	  if (!err && getsockname (conn->control, addr, &addr_len) < 0)
	    err = errno;
	}

      if (err)
	{
	  free (addr);
	  conn->actv_data_addr = 0;
	  return err;
	}
    }

  dcq = socket (AF_INET, SOCK_STREAM, 0);
  if (dcq < 0)
    return errno;

  /* Let the system choose a port for us.  */
  ((struct sockaddr_in *)addr)->sin_port = 0;

  /* Use ADDR as the data socket's local address.  */
  if (!err && bind (dcq, addr, addr_len) < 0)
    err = errno;

  /* See what port was chosen by the system.  */
  if (!err && getsockname (dcq, addr, &addr_len) < 0)
    err = errno;

  /* Set the incoming connection queue length.  */
  if (!err && listen (dcq, 1) < 0)
    err = errno;

  if (err)
    close (dcq);
  else
    err = ftp_conn_send_actv_addr (conn, conn->actv_data_addr);

  if (! err)
    *data = dcq;

  return err;
}

/* Finish opening the active data connection *DATA opened with
   ftp_conn_start_open_actv_data, following the sending of the command that
   uses the connection to the server.  This function closes the file
   descriptor in *DATA, and returns a new file descriptor for the actual data
   connection.  */
static error_t
ftp_conn_finish_open_actv_data (struct ftp_conn *conn, int *data)
{
  struct sockaddr_in rmt_addr;
  socklen_t rmt_addr_len = sizeof rmt_addr;
  int real = accept (*data, &rmt_addr, &rmt_addr_len);

  close (*data);

  if (real < 0)
    return errno;

  *data = real;

  return 0;
}

/* Abort an active data connection open sequence; this function should be
   called if ftp_conn_start_open_actv_data succeeds, but an error happens
   before ftp_conn_finish_open_actv_data can be called.  */
static void
ftp_conn_abort_open_actv_data (struct ftp_conn *conn, int data)
{
  close (data);
}

/* Return a data connection, which may not be in a completely open state;
   this call should be followed by the command that uses the connection, and
   a call to ftp_conn_finish_open_data, if that succeeds.  */
static error_t
ftp_conn_start_open_data (struct ftp_conn *conn, int *data)
{
  error_t err;

  if (conn->use_passive)
    /* First try a passive connection.  */
    {
      struct sockaddr *addr;

      /* Tell the server we wan't to use passive mode, for which it should
	 give us an address to connect to.  */
      err = ftp_conn_get_pasv_addr (conn, &addr);

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
    }
  else
    err = EAGAIN;

  if (err)
    /* Using a passive connection didn't work, try an active one.  */
    {
      conn->use_passive = 0;	/* Don't try again.  */
      err = ftp_conn_start_open_actv_data (conn, data);
    }

  return err;
}

/* Finish opening the data connection *DATA opened with
   ftp_conn_start_open_data, following the sending of the command that uses
   the connection to the server.  This function may change *DATA, in which
   case the old file descriptor is closed.  */
static error_t
ftp_conn_finish_open_data (struct ftp_conn *conn, int *data)
{
  if (conn->use_passive)
    /* Passive connections should already have been completely opened.  */
    return 0;
  else
    return ftp_conn_finish_open_actv_data (conn, data);
}

/* Abort a data connection open sequence; this function should be called if
   ftp_conn_start_open_data succeeds, but an error happens before
   ftp_conn_finish_open_data can be called.  */
static void
ftp_conn_abort_open_data (struct ftp_conn *conn, int data)
{
  if (conn->use_passive)
    close (data);
  else
    return ftp_conn_abort_open_actv_data (conn, data);
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
  error_t err = ftp_conn_start_open_data (conn, data);

  if (! err)
    {
      int reply;
      const char *txt;

      err = ftp_conn_cmd (conn, cmd, arg, &reply, &txt);
      if (!err && !REPLY_IS_PRELIM (reply))
	err = unexpected_reply (conn, reply, txt, poss_errs);

      if (err)
	ftp_conn_abort_open_data (conn, *data);
      else
	err = ftp_conn_finish_open_data (conn, data);
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
