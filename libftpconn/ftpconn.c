/* Manage an ftp connection

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
#include <string.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>

#include <arpa/telnet.h>

#include <ftpconn.h>

/* Ftp reply codes.  */
#define REPLY_DELAY	120	/* Service ready in nnn minutes */

#define REPLY_OK	200	/* Command OK */
#define REPLY_SYSTYPE	215	/* NAME version */
#define REPLY_HELLO	220	/* Service ready for new user */
#define REPLY_TRANS_OK	226	/* Closing data connection; requested file
				   action successful */
#define REPLY_PASV_OK	227	/* Entering passive mode */
#define REPLY_LOGIN_OK	230	/* User logged in, proceed */
#define REPLY_FCMD_OK	250	/* Requested file action okay, completed */
#define REPLY_DIR_NAME	257	/* "DIR" msg */

#define REPLY_NEED_PASS	331	/* User name okay, need password */
#define REPLY_NEED_ACCT 332	/* Need account for login */

#define REPLY_CLOSED	421	/* Service not available, closing control connection */
#define REPLY_ABORTED	426	/* Connection closed; transfer aborted */

#define REPLY_BAD_CMD	500	/* Syntax error; command unrecognized */
#define REPLY_BAD_ARG	501	/* Synax error in parameters or arguments */
#define REPLY_UNIMP_CMD	502	/* Command not implemented */
#define REPLY_UNIMP_ARG	504	/* Command not implemented for that parameter */

#define REPLY_NO_LOGIN	530	/* Not logged in */
#define REPLY_NO_ACCT	532	/* Need account for storing files */
#define REPLY_NO_SPACE	552	/* Requested file action aborted
				   Exceeded storage allocation */

#define REPLY_IS_PRELIM(rep) ((rep) >= 100 && (rep) < 200)
#define REPLY_IS_SUCCESS(rep) ((rep) >= 200 && (rep) < 300)
#define REPLY_IS_INCOMPLETE(rep) ((rep) >= 300 && (rep) < 400)
#define REPLY_IS_TRANSIENT(rep) ((rep) >= 400 && (rep) < 500)
#define REPLY_IS_FAILURE(rep) ((rep) >= 500 && (rep) < 600)

static error_t
unexpected_reply (struct ftp_conn *conn, int reply, const char *reply_txt,
		  const error_t *poss_errs)
{
  if (reply == REPLY_CLOSED)
    return EPIPE;
  else if (reply == REPLY_UNIMP_CMD || reply == REPLY_UNIMP_ARG)
    return EOPNOTSUPP;
  else if (reply == REPLY_BAD_ARG)
    return EINVAL;
  else if (REPLY_IS_FAILURE (reply) && reply_txt
	   && conn->syshooks.interp_err && poss_errs)
    return (*conn->syshooks.interp_err) (conn, reply_txt, poss_errs);
  else if (REPLY_IS_TRANSIENT (reply))
    return EAGAIN;
  else
    return EGRATUITOUS;
}

/* Add STR (of size LEN) to CONN's reply_txt buffer, at offset *OFFS,
   updating *OFFS.  */
static error_t
ftp_conn_add_reply_txt (struct ftp_conn *conn, size_t *offs,
			const char *str, size_t len)
{
  if (*offs + len + 1 > conn->reply_txt_sz)
    {
      size_t new_sz = *offs + len + 50;
      char *new = realloc (conn->reply_txt, new_sz);
      if (! new)
	return ENOMEM;
      conn->reply_txt = new;
      conn->reply_txt_sz = new_sz;
    }

  bcopy (str, conn->reply_txt + *offs, len);
  conn->reply_txt[*offs + len] = '\0'; /* Make sure nul terminated.  */

  *offs += len;

  return 0;
}

/* Return a new line read from CONN's control connection in LINE & LINE_LEN;
   LINE points into storage allocated in CONN, and is only valid until the
   next call to this function, or return an error code.  (we used to just use
   the stdio getline function, and keep a stdio stream for the control
   connection, but interleaved I/O didn't work correctly.)  */
static error_t
ftp_conn_getline (struct ftp_conn *conn, const char **line, size_t *line_len)
{
  char *l = conn->line;
  size_t offs = conn->line_offs, len = conn->line_len, sz = conn->line_sz;

  for (;;)
    {
      int rd;

      if (offs < len)
	/* See if there's a newline in the active part of the line buffer. */
	{
	  char *nl = memchr (l + offs, '\n', len - offs);
	  if (nl)
	    /* There is!  Consume and return the whole line we found. */
	    {
	      *line = l + offs;

	      offs = nl + 1 - l; /* Consume the line */

	      /* Null terminate the result by overwriting the newline; if
		 there's a CR preceeding it, get rid of that too.  */
	      if (nl > *line && nl[-1] == '\r')
		nl--;
	      *nl = '\0';

	      *line_len = nl - *line;

	      if (offs == len)
		conn->line_offs = conn->line_len = 0;
	      else
		conn->line_offs = offs;

	      return 0;
	    }
	}

      /* No newline yet, so read some more!  */

      if (offs > (len << 2) && offs < len)
	/* Relocate the current contents of the buffer to the beginning. */
	{
	  len -= offs;
	  bcopy (l + offs, l, len - offs);
	  offs = conn->line_offs = 0;
	  conn->line_len = len;
	}
      if (len == sz)
	/* Grow the line buffer; there's no space left.  */
	{
	  sz = sz + len ?: 50;
	  l = realloc (l, sz);
	  if (! l)
	    return ENOMEM;
	  conn->line = l;
	  conn->line_sz = sz;
	}

      /* Actually read something.  */
      rd = read (conn->control, l + len, sz - len);
      if (rd < 0)
	return errno;
      else if (rd == 0)
	{
	  *line = l + offs;
	  *line_len = 0;
	  return 0;
	}

      len += rd;
      conn->line_len = len;
    }
}

/* Get the next reply from CONN's ftp server, returning the reply code in
   REPLY, if REPLY is non-zero, and the text of the reply (not including the
   reply code) in REPLY_TXT (if it isn't zero), or return an error code.  If
   the reply is multiple lines, all of them are included in REPLY_TXT,
   separated by newlines.  */
error_t
ftp_conn_get_reply (struct ftp_conn *conn, int *reply, const char **reply_txt)
{
  size_t reply_txt_offs = 0;	/* End of a multi-line reply in accum buf.  */
  int multi = 0;		/* If a multi-line reply, the reply code. */

  if (!reply && !reply_txt)
    return 0;			/* nop */

  do
    {
      const char *l;
      size_t len;
      error_t err = ftp_conn_getline (conn, &l, &len);

      if (err)
	return err;
      if (!multi && len == 0)
	return EPIPE;

#define ACCUM(txt, len)								\
  do {										\
    if (reply_txt)		/* Only accumulate if wanted.  */		\
      {										\
	error_t err = ftp_conn_add_reply_txt (conn, &reply_txt_offs, txt, len);	\
	if (err)								\
	  return err;								\
      }										\
  } while (0)

      if (conn->hooks && conn->hooks->cntl_debug)
	(*conn->hooks->cntl_debug) (conn, FTP_CONN_CNTL_DEBUG_REPLY, l);

      if (isdigit (l[0]) && isdigit (l[1]) && isdigit (l[2]))
	/* A reply code.  */
	{
	  int code = (l[0] - '0')*100 + (l[1] - '0')*10 + (l[2] - '0');

	  if (multi && code != multi)
	    /* Two codes in a multi-line reply don't match.  */
	    return EGRATUITOUS;

	  if (l[3] == '-')
	    /* The non-terminal line of a multi-line reply.  RFC959 actually
	       claims there shouldn't be more than one multi-line code (other
	       lines in between the two shouldn't have a numeric code at
	       all), but real ftp servers don't obey this rule. */
	    multi = code;
	  else if (l[3] != ' ')
	    /* Some syntax error.  */
	    return EGRATUITOUS;
	  else
	    /* The end of the reply (and perhaps the only line).  */
	    {
	      multi = 0;
	      if (reply)
		*reply = code;
	    }

	  ACCUM (l + 4, len - 4);
	}
      else if (multi)
	/* The lines between the first and last in a multi-line reply may be
	   anything as long as they don't start with a digit.  */
	ACCUM (l, len);
      else
	return EGRATUITOUS;
    }
  while (multi);

  if (reply_txt)
    *reply_txt = conn->reply_txt;

  return 0;
}

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
	  error_t err;
	  do
	    err = ftp_conn_get_reply (conn, &reply, 0);
	  while (reply == REPLY_ABORTED);
	  if (reply != REPLY_TRANS_OK)
	    ftp_conn_close (conn);
	}
      else
	ftp_conn_close (conn);
    }
}

error_t
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
	if (pass)
	  err = ftp_conn_cmd (conn, "pass", pass, &reply, 0);
	else
	  {
	    pass = getenv ("USER");
	    if (pass)
	      pass = getenv ("LOGNAME");
	    if (pass)
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
      if (pass && !p->pass)
	free (pass);
    }

  if (!err && reply != REPLY_LOGIN_OK)
    if (REPLY_IS_FAILURE (reply))
      err = EACCES;
    else
      err = unexpected_reply (conn, reply, 0, 0);

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
    if (reply == REPLY_SYSTYPE || reply == REPLY_BAD_CMD)
      {
	if (reply == REPLY_BAD_CMD)
	  txt = 0;
	if (conn->hooks && conn->hooks->choose_syshooks)
	  (*conn->hooks->choose_syshooks) (conn, txt);
	else
	  ftp_conn_choose_syshooks (conn, txt);
      }
    else
      err = unexpected_reply (conn, reply, txt, 0);

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
  bzero (&conn->syshooks, sizeof conn->syshooks);

  csock = socket (PF_INET, SOCK_STREAM, 0);
  if (csock < 0)
    return errno;

  ftp_addr.sin_len = conn->params->addr_len;
  ftp_addr.sin_family = conn->params->addr_type;
  ftp_addr.sin_addr = *(struct in_addr *)conn->params->addr;
  ftp_addr.sin_port = ftp_port;

  if (connect (csock, &ftp_addr, sizeof ftp_addr) < 0)
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
    err = ftp_conn_sysify (conn);

  if (! err)
    /* login */
    err = ftp_conn_login (conn);

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
  new->cwd = 0;
  new->type = 0;
  bzero (&new->syshooks, sizeof new->syshooks);

  if (new->hooks && new->hooks->init)
    err = (*new->hooks->init) (new);
  else
    err = 0;

  if (! err)
    err = ftp_conn_open (new);

  if (err)
    ftp_conn_free (new);
  else
    *conn = new;

  return err;

}

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
  free (conn);
}

static error_t
ftp_conn_get_pasv_addr (struct ftp_conn *conn, struct sockaddr **addr)
{
  int reply;
  const char *txt;
  error_t err = ftp_conn_cmd_reopen (conn, "pasv", 0, &reply, &txt);

  if (! err)
    if (reply == REPLY_PASV_OK)
      err = (*(conn->syshooks.pasv_addr ?: ftp_conn_unix_pasv_addr)) (conn, txt, addr);
    else
      err = unexpected_reply (conn, reply, txt, 0);

  return err;
}

static error_t
ftp_conn_send_actv_addr (struct ftp_conn *conn, struct sockaddr *addr)
{
  error_t err;

  if (addr == 0)
    err = EINVAL;
  else if (addr->sa_family != AF_INET)
    err = EAFNOSUPPORT;
  else
    {
      char buf[50];
      int reply;
      unsigned char *a =
	(unsigned char *)&((struct sockaddr_in *)addr)->sin_addr.s_addr;
      unsigned char *p =
	(unsigned char *)&((struct sockaddr_in *)addr)->sin_port;

      snprintf (buf, sizeof buf, "%d,%d,%d,%d,%d,%d", 
		a[0], a[1], a[2], a[3], p[0], p[1]);
      err = ftp_conn_cmd_reopen (conn, "port", buf, &reply, 0);

      if (! err)
	if (reply == REPLY_OK)
	  err = 0;
	else
	  err = unexpected_reply (conn, reply, 0, 0);
    }

  return err;
}

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

static const error_t poss_file_errs[] = {
  EIO, ENOENT, EPERM, EACCES, ENOTDIR, ENAMETOOLONG, ELOOP, EISDIR, EROFS,
  EMFILE, ENFILE, ENXIO, EOPNOTSUPP, ENOSPC, EDQUOT, ETXTBSY, EEXIST,
  0
};

/* Start retreiving file NAME over CONN, returning a file descriptor in DATA
   over which the data can be read.  */
error_t
ftp_conn_start_retrieve (struct ftp_conn *conn, const char *name, int *data)
{
  if (! name)
    return EINVAL;
  return ftp_conn_start_transfer (conn, "retr", name, poss_file_errs, data);
}

/* Start retreiving a list of files in NAME over CONN, returning a file
   descriptor in DATA over which the data can be read.  */
error_t
ftp_conn_start_list (struct ftp_conn *conn, const char *name, int *data)
{
  return ftp_conn_start_transfer (conn, "nlst", name, poss_file_errs, data);
}

/* Start retreiving a directory listing of NAME over CONN, returning a file
   descriptor in DATA over which the data can be read.  */
error_t
ftp_conn_start_dir (struct ftp_conn *conn, const char *name, int *data)
{
  return ftp_conn_start_transfer (conn, "list", name, poss_file_errs, data);
}

/* Start storing into file NAME over CONN, returning a file descriptor in DATA
   into which the data can be written.  */
error_t
ftp_conn_start_store (struct ftp_conn *conn, const char *name, int *data)
{
  if (! name)
    return EINVAL;
  return ftp_conn_start_transfer (conn, "stor", name, poss_file_errs, data);
}

/* Transfer the output of SRC_CMD/SRC_NAME on SRC_CONN to DST_NAME on
   DST_CONN, moving the data directly between servers.  */
error_t
ftp_conn_rmt_transfer (struct ftp_conn *src_conn,
		       const char *src_cmd, const char *src_name,
		       const int *src_poss_errs,
		       struct ftp_conn *dst_conn, const char *dst_name)
{
  struct sockaddr *src_addr;
  error_t err = ftp_conn_get_pasv_addr (src_conn, &src_addr);

  if (! err)
    {
      err = ftp_conn_send_actv_addr (dst_conn, src_addr);

      if (! err)
	{
	  int reply;
	  const char *txt;
	  err = ftp_conn_cmd (src_conn, src_cmd, src_name, 0, 0);

	  if (! err)
	    {
	      err = ftp_conn_cmd (dst_conn, "stor", dst_name, &reply, &txt);

	      if (! err)
		if (REPLY_IS_PRELIM (reply))
		  {
		    err = ftp_conn_get_reply (src_conn, &reply, &txt);
		    if (!err && !REPLY_IS_PRELIM (reply))
		      err = unexpected_reply (src_conn, reply, txt, src_poss_errs);

		    if (err)
		      ftp_conn_abort (dst_conn);
		    else
		      err = ftp_conn_finish_transfer (dst_conn);
		  }
		else
		  err = unexpected_reply (dst_conn, reply, txt, poss_file_errs);

	      if (err)
		ftp_conn_abort (src_conn);
	      else
		err = ftp_conn_finish_transfer (src_conn);
	    }
	}

      free (src_addr);
    }

  return err;
}

/* Copy the SRC_NAME on SRC_CONN to DST_NAME on DST_CONN, moving the data
   directly between servers.  */
error_t
ftp_conn_rmt_copy (struct ftp_conn *src_conn, const char *src_name,
		   struct ftp_conn *dst_conn, const char *dst_name)
{
  return ftp_conn_rmt_transfer (src_conn, "retr", src_name, poss_file_errs,
				dst_conn, dst_name);
}

static error_t
_cache_cwd (struct ftp_conn *conn, int reopen)
{
  int reply;
  const char *txt;
  error_t err =
    (reopen ? ftp_conn_cmd_reopen : ftp_conn_cmd) (conn, "pwd", 0, &reply, &txt);

  if (! err)
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
	if (reply == REPLY_FCMD_OK)
	  err = _cache_cwd (conn, 0);
	else
	  err = unexpected_reply (conn, reply, txt, poss_file_errs);
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
    if (reply == REPLY_OK)
      err = _cache_cwd (conn, 0);
    else
      err = unexpected_reply (conn, reply, txt, poss_file_errs);
  return err;
}

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
	  error_t err = ftp_conn_cmd_reopen (conn, "type", type, &reply, 0);
	  if (! err)
	    if (reply == REPLY_OK)
	      {
		if (conn->type)
		  free ((char *)conn->type);
		conn->type = type;
	      }
	    else
	      err = unexpected_reply (conn, reply, 0, 0);
	}
    }    

  return err;
}

/* Start an operation to get a list of file-stat structures for NAME (this
   is often similar to ftp_conn_start_dir, but with OS-specific flags), and
   return a file-descriptor for reading on, and a state structure in STATE
   suitable for passing to cont_get_stats.  FORCE_DIR controls what happens if
   NAME refers to a directory: if FORCE_DIR is false, STATS will contain
   entries for all files *in* NAME, and if FORCE_DIR is true, it will
   contain just a single entry for NAME itself (or an error will be
   returned when this isn't possible).  */
error_t
ftp_conn_start_get_stats (struct ftp_conn *conn,
			  const char *name, int force_dir,
			  int *fd, void **state)
{
  if (conn->syshooks.start_get_stats)
    return
      (*conn->syshooks.start_get_stats) (conn, name, force_dir, fd, state);
  else
    return EOPNOTSUPP;
}

/* Read stats information from FD, calling ADD_STAT for each new stat (HOOK
   is passed to ADD_STAT).  FD and STATE should be returned from
   start_get_stats.  If this function returns EAGAIN, then it should be
   called again to finish the job (possibly after calling select on FD); if
   it returns 0, then it is finishe,d and FD and STATE are deallocated.  */
error_t
ftp_conn_cont_get_stats (struct ftp_conn *conn, int fd, void *state,
			 ftp_conn_add_stat_fun_t add_stat, void *hook)
{
  if (conn->syshooks.cont_get_stats)
    return (*conn->syshooks.cont_get_stats) (conn, fd, state, add_stat, hook);
  else
    return EOPNOTSUPP;
}

/* Get a list of file-stat structures for NAME, calling ADD_STAT for each one
   (HOOK is passed to ADD_STAT).  If NAME refers to an ordinary file, a
   single entry for it is returned for it; if NAME refers to a directory,
   then if FORCE_DIR is false, STATS will contain entries for all files *in*
   NAME, and if FORCE_DIR is true, it will contain just a single entry for
   NAME itself (or an error will be returned when this isn't possible).  This
   function may block.  */
error_t
ftp_conn_get_stats (struct ftp_conn *conn,
		    const char *name, int force_dir,
		    ftp_conn_add_stat_fun_t add_stat, void *hook)
{
  int fd;
  void *state;
  error_t err = ftp_conn_start_get_stats (conn, name, force_dir, &fd, &state);

  if (err)
    return err;

  do
    err = ftp_conn_cont_get_stats (conn, fd, state, add_stat, hook);
  while (err == EAGAIN);

  return err;
}

