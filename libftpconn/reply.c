/* Parse ftp server replies

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
#include <ctype.h>

#include <ftpconn.h>
#include "priv.h"

/* Add STR (of size LEN) to CONN's reply_txt buffer, at offset *OFFS,
   updating *OFFS.  */
static inline error_t
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
static inline error_t
ftp_conn_getline (struct ftp_conn *conn, const char **line, size_t *line_len)
{
  char *l = conn->line;
  size_t offs = conn->line_offs, len = conn->line_len, sz = conn->line_sz;
  int (*icheck) (struct ftp_conn *conn) = conn->hooks->interrupt_check;

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
		 there's a CR preceding it, get rid of that too.  */
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

      if (icheck && (*icheck) (conn))
	return EINTR;
    }
}

/* Get the next reply from CONN's ftp server, returning the reply code in
   REPLY, if REPLY is non-zero, and the text of the reply (not including the
   reply code) in REPLY_TXT (if it isn't zero), or return an error code.  If
   the reply is multiple lines, all of them are included in REPLY_TXT,
   separated by newlines.  */
inline error_t
ftp_conn_get_raw_reply (struct ftp_conn *conn, int *reply,
			const char **reply_txt)
{
  size_t reply_txt_offs = 0;	/* End of a multi-line reply in accum buf.  */
  int multi = 0;		/* If a multi-line reply, the reply code. */

  if (!reply && !reply_txt)
    return 0;			/* nop */

  do
    {
      const char *l = NULL;
      size_t len = 0;
      error_t err = ftp_conn_getline (conn, &l, &len);

      if (err)
	return err;
      if (!multi && len == 0)
	return EPIPE;

#define ACCUM(txt, len)							      \
  do {									      \
    if (reply_txt)		/* Only accumulate if wanted.  */	      \
      {									      \
	error_t err =							      \
	  ftp_conn_add_reply_txt (conn, &reply_txt_offs, txt, len);	      \
	if (err)							      \
	  return err;							      \
      }									      \
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

/* Get the next reply from CONN's ftp server, returning the reply code in
   REPLY, if REPLY is non-zero, and the text of the reply (not including the
   reply code) in REPLY_TXT (if it isn't zero), or return an error code.  If
   the reply is multiple lines, all of them are included in REPLY_TXT,
   separated by newlines.  This differs from ftp_conn_get_raw_reply in that
   it eats REPLY_ABORT_OK replies on the assumption that they're junk left
   over from the last abort command.  */
error_t
ftp_conn_get_reply (struct ftp_conn *conn, int *reply, const char **reply_txt)
{
  int code;
  error_t err;

  do
    err = ftp_conn_get_raw_reply (conn, &code, reply_txt);
  while (!err && code == REPLY_ABORT_OK);

  if (!err && reply)
    *reply = code;

  return err;
}
