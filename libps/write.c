/* Ps stream output

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "ps.h"
#include "common.h"

/* ---------------------------------------------------------------- */

/* Write at most MAX_LEN characters of STRING to STREAM (if MAX_LEN > the
   length of STRING, then write all of it; if MAX_LEN == -1, then write all
   of STRING regardless).  */
error_t
ps_stream_write (ps_stream_t stream, char *string, int max_len)
{
  int len = strlen(string);

  if (max_len > 0 && len > max_len)
    len = max_len;

  if (len > 0)
    {
      int output;
      int spaces_needed = stream->spaces;

      stream->spaces = 0;
      while (spaces_needed > 0)
	{
	  static char spaces[] = "                                ";
#define spaces_len (sizeof(spaces) - 1)
	  int chunk = spaces_needed > spaces_len ? spaces_len : spaces_needed;
	  error_t err =
	    ps_stream_write (stream, spaces + spaces_len - chunk, chunk);
	  if (err)
	    return err;
	  spaces_needed -= chunk;
	}
      stream->spaces = spaces_needed;

      output = fwrite (string, 1, len, stream->stream);
      if (output == 0)
	return errno;

      stream->pos += len;
    }

  return 0;
}

/* Write NUM spaces to STREAM.  NUM may be negative, in which case the same
   number of adjacent spaces (written by other calls to ps_stream_space) are
   consumed if possible.  If an error occurs, the error code is returned,
   otherwise 0.  */
error_t
ps_stream_space (ps_stream_t stream, int num)
{
  stream->spaces += num;
  return 0;
}

/* Write as many spaces to STREAM as required to make a field of width SOFAR
   be at least WIDTH characters wide (the absolute value of WIDTH is used).
   If an error occurs, the error code is returned, otherwise 0.  */
error_t
ps_stream_pad (ps_stream_t stream, int sofar, int width)
{
  return ps_stream_space (stream, ABS (width) - sofar);
}

/* Write a newline to STREAM, resetting its position to zero.  */
error_t
ps_stream_newline (ps_stream_t stream)
{
  putc ('\n', stream->stream);
  stream->spaces = 0;
  stream->pos = 0;
  return 0;
}

/* Write the string BUF to STREAM, padded on one side with spaces to be at
   least the absolute value of WIDTH long: if WIDTH >= 0, then on the left
   side, otherwise on the right side.  If an error occurs, the error code is
   returned, otherwise 0.  */
error_t
ps_stream_write_field (ps_stream_t stream, char *buf, int width)
{
  int len;
  error_t err;

  while (isspace (*buf))
    buf++;

  len = strlen(buf);
  while (isspace (buf[len - 1]))
    len--;

  if (width > 0)
    {
      err = ps_stream_write (stream, buf, len);
      if (!err)
	err = ps_stream_space (stream, width - len);
    }
  else if (width < 0)
    {
      err = ps_stream_space (stream, -width - len);
      if (!err)
	err = ps_stream_write (stream, buf, len);
    }
  else
    err = ps_stream_write (stream, buf, len);

  return err;
}

/* Write the decimal representation of VALUE to STREAM, padded on one side
   with spaces to be at least the absolute value of WIDTH long: if WIDTH >=
   0, then on the left side, otherwise on the right side.  If an error
   occurs, the error code is returned, otherwise 0.  */
error_t
ps_stream_write_int_field (ps_stream_t stream, int value, int width)
{
  char buf[20];
  sprintf(buf, "%d", value);
  return ps_stream_write_field (stream, buf, width);
}

/* Create a stream outputing to DEST, and return it in STREAM, or an error.  */
error_t
ps_stream_create (FILE *dest, ps_stream_t *stream)
{
  *stream = malloc (sizeof (struct ps_stream));
  if (! *stream)
    return ENOMEM;
  (*stream)->stream = dest;
  (*stream)->spaces = 0;
  (*stream)->pos = 0;
  return 0;
}

/* Frees STREAM.  The destination file is *not* closed.  */
void
ps_stream_free (ps_stream_t stream)
{
  free (stream);
}
