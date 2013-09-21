/* Ps stream output

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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

/* True if CH is a `control character'.  */
#define iscntl(ch) ((unsigned)(ch) < 32)

/* *BEG and NEW - 1 are the bounds of a buffer, which write to S (the last
   character just before NEW isn't included, because something different
   about it is what caused the flush), and update *BEG to be NEW.  True is
   returned if a write error occurs.  */
static int
flush (const char **beg, const char *new, FILE *s)
{
  const char *b = *beg;
  if (new > b)
    *beg = new;
  if (new - 1 > b)
    {
      size_t len = new - 1 - b;
      int ret = fwrite (b, 1, len, s);
      if (ret < len)
	return 1;
    }
  return 0;
}

/* Write T to S, up to MAX characters (unless MAX == 0), making sure not to
   write any unprintable characters.  */ 
error_t
noise_write (const char *t, ssize_t max, FILE *s)
{
  int ch;
  const char *ok = t;
  size_t len = 0;

  while ((ch = *t++) && (max < 0 || len < max))
    if (isgraph (ch) || ch == ' ')
      len++;
    else
      {
	int is_cntl = iscntl (ch);

	if (flush (&ok, t, s))
	  return errno;

	len += (is_cntl ? 2 : 4);
	if (max >= 0 && len > max)
	  break;

	if (is_cntl)
	  fprintf (s, "^%c", ch + 'A');
	else
	  fprintf (s, "\\%03o", ch);
      }

  if (flush (&ok, t, s))
    return errno;

  return 0;
}

/* Return what noise_write would write with arguments of T and MAX.  */
size_t
noise_len (const char *t, ssize_t max)
{
  int ch;
  size_t len = 0;

  while ((ch = *t++) && (max == 0 || len < max))
    if (isgraph (ch) || ch == ' ')
      len++;
    else
      {
	size_t rep_len = iscntl (ch) ? 2 : 4;
	if (max >= 0 && rep_len + len > max)
	  break;
	len += rep_len;
      }

  return len;
}

/* ---------------------------------------------------------------- */

/* Write at most MAX_LEN characters of STRING to STREAM (if MAX_LEN > the
   length of STRING, then write all of it; if MAX_LEN == -1, then write all
   of STRING regardless).  */
error_t
ps_stream_write (struct ps_stream *stream, const char *string, ssize_t max_len)
{
  size_t len = noise_len (string, max_len);

  if (len > 0)
    {
      error_t err;
      ssize_t spaces_needed = stream->spaces;

      stream->spaces = 0;
      while (spaces_needed > 0)
	{
	  static char spaces[] = "                                ";
#define spaces_len (sizeof(spaces) - 1)
	  size_t chunk = spaces_needed > spaces_len ? spaces_len : spaces_needed;
	  error_t err =
	    ps_stream_write (stream, spaces + spaces_len - chunk, chunk);
	  if (err)
	    return err;
	  spaces_needed -= chunk;
	}
      stream->spaces = spaces_needed;

      err = noise_write (string, len, stream->stream);
      if (err)
	return err;

      stream->pos += len;
    }

  return 0;
}

/* Write NUM spaces to STREAM.  NUM may be negative, in which case the same
   number of adjacent spaces (written by other calls to ps_stream_space) are
   consumed if possible.  If an error occurs, the error code is returned,
   otherwise 0.  */
error_t
ps_stream_space (struct ps_stream *stream, ssize_t num)
{
  stream->spaces += num;
  return 0;
}

/* Write as many spaces to STREAM as required to make a field of width SOFAR
   be at least WIDTH characters wide (the absolute value of WIDTH is used).
   If an error occurs, the error code is returned, otherwise 0.  */
error_t
ps_stream_pad (struct ps_stream *stream, ssize_t sofar, ssize_t width)
{
  return ps_stream_space (stream, ABS (width) - sofar);
}

/* Write a newline to STREAM, resetting its position to zero.  */
error_t
ps_stream_newline (struct ps_stream *stream)
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
_ps_stream_write_field (struct ps_stream *stream,
			const char *buf, size_t max_width,
			int width)
{
  error_t err;
  size_t len;

  while (isspace (*buf))
    buf++;

  if (stream->spaces < 0 && max_width >= 0)
    /* Take some of our spacing deficit out of a truncatable field.  */
    max_width += stream->spaces;

  len = noise_len (buf, max_width);

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

/* Write the string BUF to STREAM, padded on one side with spaces to be at
   least the absolute value of WIDTH long: if WIDTH >= 0, then on the left
   side, otherwise on the right side.  If an error occurs, the error code is
   returned, otherwise 0.  */
error_t
ps_stream_write_field (struct ps_stream *stream, const char *buf, int width)
{
  return _ps_stream_write_field (stream, buf, -1, width);
}

/* Like ps_stream_write_field, but truncates BUF to make it fit into WIDTH.  */
error_t
ps_stream_write_trunc_field (struct ps_stream *stream,
			     const char *buf, int width)
{
  return _ps_stream_write_field (stream, buf, width ? ABS (width) : -1, width);
}

/* Write the decimal representation of VALUE to STREAM, padded on one side
   with spaces to be at least the absolute value of WIDTH long: if WIDTH >=
   0, then on the left side, otherwise on the right side.  If an error
   occurs, the error code is returned, otherwise 0.  */
error_t
ps_stream_write_int_field (struct ps_stream *stream, int value, int width)
{
  char buf[20];
  sprintf (buf, "%d", value);
  return ps_stream_write_field (stream, buf, width);
}

/* Create a stream outputing to DEST, and return it in STREAM, or an error.  */
error_t
ps_stream_create (FILE *dest, struct ps_stream **stream)
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
ps_stream_free (struct ps_stream *stream)
{
  free (stream);
}
