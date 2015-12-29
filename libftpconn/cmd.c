/* Send commands to the ftp server

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
#include <arpa/telnet.h>

#include <ftpconn.h>
#include "priv.h"

/* Version of write that writes all LEN bytes of BUF if possible to FD.  */
static error_t
_write (int fd, const void *buf, size_t len)
{
  while (len > 0)
    {
      ssize_t wr = write (fd, buf, len);
      if (wr < 0)
	return errno;
      else if (wr == 0)
	return EPIPE;
      buf += wr;
      len -= wr;
    }
  return 0;
}

static error_t
_skip_write (int fd, const void *buf, size_t len, size_t *skip)
{
  size_t sk = *skip;
  error_t err = 0;

  if (len > sk)
    {
      err = _write (fd, buf + sk, len - sk);
      *skip = 0;
    }
  else
    *skip = sk - len;

  return err;
}

/* Ridiculous function to deal with the never-to-occur case of the ftp
   command being too long for the buffer in ftp_conn_cmd; just writes the
   portion of the command that wasn't written there.  */
static error_t
_long_cmd (int fd, const char *cmd, const char *arg, size_t skip)
{
  error_t err = _skip_write (fd, cmd, strlen (cmd), &skip);
  if (!err && arg)
    {
      err = _skip_write (fd, " ", 1, &skip);
      if (! err)
	err = _skip_write (fd, arg, strlen (arg), &skip);
    }
  if (! err)
    err = _skip_write (fd, "\r\n", 2, &skip);
  return err;
}

/* Send the ftp command CMD, with optional argument ARG (if non-zero) to
   CONN's ftp server.  If either of REPLY or REPLY_TXT is non-zero, then a
   reply is waited for and returned as with ftp_conn_get_reply, otherwise
   the next reply from the server is left unconsumed.  */
error_t
ftp_conn_cmd (struct ftp_conn *conn, const char *cmd, const char *arg,
	      int *reply, const char **reply_txt)
{
  error_t err = 0;

  if (conn->control < 0)
    err = EPIPE;
  else
    /* (This used to try to call dprintf to output to conn->control, but that
       function doesn't appear to work.) */
    {
      char buf[200];
      size_t out =
	snprintf (buf, sizeof buf, arg ? "%s %s\r\n" : "%s\r\n", cmd, arg);
      err = _write (conn->control, buf, out);

      if (!err && conn->hooks && conn->hooks->cntl_debug)
	{
	  buf[out - 2] = '\0';	/* Stomp the CR & NL.  */
	  (* conn->hooks->cntl_debug) (conn, FTP_CONN_CNTL_DEBUG_CMD, buf);
	}

      if (!err && out == sizeof buf)
	err = _long_cmd (conn->control, cmd, arg, sizeof buf);
    }

  if (!err && (reply || reply_txt))
    err = ftp_conn_get_reply (conn, reply, reply_txt);

  return err;
}

/* Send an ftp command to CONN's server, and optionally await a reply as with
   ftp_conn_cmd, but also open a new connection if it appears that the old
   one has died (as when the ftp server times it out).  */
error_t
ftp_conn_cmd_reopen (struct ftp_conn *conn, const char *cmd, const char *arg,
		      int *reply, const char **reply_txt)
{
  int _reply;
  error_t err;

  err = ftp_conn_cmd (conn, cmd, arg, &_reply, reply_txt);
  if (err == EPIPE || (!err && _reply == REPLY_CLOSED))
    /* Retry once after reopening the connection.  */
    {
      err = ftp_conn_open (conn);
      if (! err)
	err = ftp_conn_cmd (conn, cmd, arg, reply, reply_txt);
    }
  else if (reply)
    *reply = _reply;

  return err;
}

/* Send an ftp ABOR command to CONN's server, aborting any transfer in
   progress.  */
void
ftp_conn_abort (struct ftp_conn *conn)
{
  if (conn->control >= 0)
    {
      static const char ip[] = { IAC, IP, IAC };
      static const char abor[] = { DM, 'a', 'b', 'o', 'r', '\r', '\n' };

      if (conn->hooks && conn->hooks->cntl_debug)
	(* conn->hooks->cntl_debug) (conn, FTP_CONN_CNTL_DEBUG_CMD, "abor");

      if (send (conn->control, ip, sizeof ip, MSG_OOB) == sizeof ip
	  && write (conn->control, abor, sizeof abor) == sizeof abor)
	{
	  int reply;
	  do
	    ftp_conn_get_raw_reply (conn, &reply, 0);
	  while (reply == REPLY_ABORTED);
	  if (reply != REPLY_TRANS_OK && reply != REPLY_ABORT_OK)
	    ftp_conn_close (conn);
	}
      else
	ftp_conn_close (conn);
    }
}
