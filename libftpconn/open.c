/* Connection initiation

   Copyright (C) 1997, 1998, 1999 Free Software Foundation, Inc.

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
#include <ctype.h>
#include <pwd.h>
#include <netdb.h>
#include <netinet/in.h>

#include <ftpconn.h>
#include "priv.h"

static error_t
ftp_conn_login (struct ftp_conn *conn)
{
  int reply;
  error_t err = 0;
  const struct ftp_conn_params *p = conn->params;

  err = ftp_conn_cmd (conn, "user", p->user ?: "anonymous", &reply, 0);

  if (!err && reply == REPLY_NEED_ACCT)
    {
      char *acct = p->acct;
      if (!acct && conn->hooks && conn->hooks->get_login_param)
	err = (* conn->hooks->get_login_param) (conn,
						FTP_CONN_GET_LOGIN_PARAM_ACCT,
						&acct);
      if (! err)
	err = acct ? ftp_conn_cmd (conn, "acct", acct, &reply, 0) : EACCES;
      if (acct && !p->acct)
	free (acct);
    }

  if (!err && reply == REPLY_NEED_PASS)
    {
      char *pass = p->pass;
      if (!pass && conn->hooks && conn->hooks->get_login_param)
	err = (* conn->hooks->get_login_param) (conn,
						FTP_CONN_GET_LOGIN_PARAM_PASS,
						&pass);
      if (! err)
	{
	  if (pass)
	    err = ftp_conn_cmd (conn, "pass", pass, &reply, 0);
	  else
	    {
	      pass = getenv ("USER");
	      if (! pass)
		pass = getenv ("LOGNAME");
	      if (! pass)
		{
		  struct passwd *pe = getpwuid (getuid ());
		  pass = pe ? pe->pw_name : "?";
		}

	      /* Append a '@' */
	      pass = strdup (pass);
	      if (pass)
		pass = realloc (pass, strlen (pass) + 1);
	      if (pass)
		{
		  strcat (pass, "@");
		  err = ftp_conn_cmd (conn, "pass", pass, &reply, 0);
		}
	      else
		err = ENOMEM;
	    }
	}
      if (pass && !p->pass)
	free (pass);
    }

  if (!err && reply != REPLY_LOGIN_OK)
    {
      if (REPLY_IS_FAILURE (reply))
	err = EACCES;
      else
	err = unexpected_reply (conn, reply, 0, 0);
    }

  return err;
}

static error_t
ftp_conn_hello (struct ftp_conn *conn)
{
  int reply;
  error_t err;

  do
    err = ftp_conn_get_reply (conn, &reply, 0);
  while (!err && reply == REPLY_DELAY);

  if (err)
    return err;

  if (reply == REPLY_CLOSED)
    return ECONNREFUSED;
  if (reply != REPLY_HELLO)
    return EGRATUITOUS;

  return 0;
}

/* Sets CONN's syshooks to a copy of SYSHOOKS.  */
void
ftp_conn_set_syshooks (struct ftp_conn *conn, struct ftp_conn_syshooks *syshooks)
{
  conn->syshooks = *syshooks;
}

void
ftp_conn_choose_syshooks (struct ftp_conn *conn, const char *syst)
{
  if (!syst || (strncasecmp (syst, "UNIX", 4) == 0 && !isalnum (syst[4])))
    ftp_conn_set_syshooks (conn, &ftp_conn_unix_syshooks);
}

/* Sets CONN's syshooks by querying the remote system to see what type it is. */
static error_t
ftp_conn_sysify (struct ftp_conn *conn)
{
  int reply;
  const char *txt;
  error_t err = ftp_conn_cmd (conn, "syst", 0, &reply, &txt);

  if (! err)
    {
      if (reply == REPLY_SYSTYPE ||
	  reply == REPLY_BAD_CMD || reply == REPLY_UNIMP_CMD || REPLY_NO_LOGIN)
	{
	  if (reply == REPLY_BAD_CMD || reply == REPLY_UNIMP_CMD
	      || reply == REPLY_NO_LOGIN)
	    txt = 0;
	  if (conn->hooks && conn->hooks->choose_syshooks)
	    (*conn->hooks->choose_syshooks) (conn, txt);
	  else
	    ftp_conn_choose_syshooks (conn, txt);
	  conn->syshooks_valid = 1;
	}
      else
	err = unexpected_reply (conn, reply, txt, 0);
    }

  return err;
}

error_t
ftp_conn_open (struct ftp_conn *conn)
{
  static int ftp_port = 0;
  int csock;
  error_t err;
  struct sockaddr_in ftp_addr;

  if (conn->params->addr_type != AF_INET)
    return EAFNOSUPPORT;

  if (! ftp_port)
    {
      struct servent *se = getservbyname ("ftp", "tcp");
      if (! se)
	return EGRATUITOUS;
      ftp_port = se->s_port;
    }

  if (conn->control >= 0)
    {
      close (conn->control);
      conn->control = -1;
    }
  memset (&conn->syshooks, 0, sizeof conn->syshooks);

  csock = socket (PF_INET, SOCK_STREAM, 0);
  if (csock < 0)
    return errno;

  ftp_addr.sin_len = sizeof ftp_addr;
  ftp_addr.sin_family = conn->params->addr_type;
  ftp_addr.sin_addr = *(struct in_addr *)conn->params->addr;
  ftp_addr.sin_port = ftp_port;

  if (connect (csock, (struct sockaddr *)&ftp_addr, sizeof ftp_addr) < 0)
    {
      err = errno;
      close (csock);
      return err;
    }

  conn->control = csock;

  err = ftp_conn_hello (conn);

  if (!err && conn->hooks && conn->hooks->opened)
    (* conn->hooks->opened) (conn);

  if (! err)
    /* Make any machine-dependent customizations.  */
    ftp_conn_sysify (conn);

  if (! err)
    /* login */
    err = ftp_conn_login (conn);

  if (!err && !conn->syshooks_valid)
    /* Try again now. */
    err = ftp_conn_sysify (conn);

  if (!err && conn->type)
    /* Set the connection type.  */
    {
      int reply;
      err = ftp_conn_cmd (conn, "type", conn->type, &reply, 0);
      if (!err && reply != REPLY_OK)
	err = unexpected_reply (conn, reply, 0, 0);
    }

  if (err)
    ftp_conn_close (conn);

  return err;
}

void
ftp_conn_close (struct ftp_conn *conn)
{
  if (conn->control >= 0)
    close (conn->control);
  conn->control = -1;
  if (conn->hooks && conn->hooks->closed)
    (* conn->hooks->closed) (conn);
}
