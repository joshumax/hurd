/* Some helper functions for writing output fields.

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
#include <errno.h>

#include "ps.h"
#include "common.h"

/* ---------------------------------------------------------------- */

/* Write at most MAX_LEN characters of STRING to STREAM (if MAX_LEN > the
   length of STRING, then write all of it; if MAX_LEN == -1, then write all
   of STRING regardless).  If COUNT is non-NULL, the number of characters
   written is added to the integer it points to.  If an error occurs, the
   error code is returned, otherwise 0.  */
error_t
ps_write_string(char *string, int max_len, FILE *stream, int *count)
{
  int len = strlen(string);

  if (max_len > 0 && len > max_len)
    len = max_len;

  if (len > 0)
    {
      int output = fwrite(string, 1, len, stream);
      if (output == 0)
	return errno;
      if (count)
	*count += output;
    }

  return 0;
}

/* Write NUM spaces to STREAM.  If COUNT is non-NULL, the number of spaces
   written is added to the integer it points to.  If an error occurs, the
   error code is returned, otherwise 0.  */
error_t
ps_write_spaces(int num, FILE *stream, int *count)
{
  static char spaces[] = "                                ";
#define spaces_len (sizeof(spaces) - 1)

  while (num > spaces_len)
    {
      error_t err = ps_write_string(spaces, spaces_len, stream, count);
      if (err)
	return err;
      num -= spaces_len;
    }

  if (num > 0)
    return ps_write_string(spaces, num, stream, count);
  else
    return 0;
}

/* Write as many spaces to STREAM as required to make a field of width SOFAR
   be at least WIDTH characters wide (the absolute value of WIDTH is used).
   If COUNT is non-NULL, the number of spaces written is added to the integer
   it points to.  If an error occurs, the error code is returned, otherwise
   0.  */
error_t
ps_write_padding(int sofar, int width, FILE *stream, int *count)
{
  width = ABS(width);
  if (sofar < width)
    return ps_write_spaces(width - sofar, stream, count);
  else
    return 0;
}

/* Write the string BUF to STREAM, padded on one side with spaces to be at
   least the absolute value of WIDTH long: if WIDTH >= 0, then on the left
   side, otherwise on the right side.  If COUNT is non-NULL, the number of
   characters written is added to the integer it points to.  If an error
   occurs, the error code is returned, otherwise 0.  */
error_t
ps_write_field(char *buf, int width, FILE *stream, int *count)
{
  error_t err;
  int len = strlen(buf);

  if (width > len)
    {
      err = ps_write_string(buf, -1, stream, count);
      if (!err)
	err = ps_write_spaces(width - len, stream, count);
    }
  else if (-width > len)
    {
      err = ps_write_spaces(-width - len, stream, count);
      if (!err)
	err = ps_write_string(buf, -1, stream, count);
    }
  else
    err = ps_write_string(buf, -1, stream, count);

  return err;
}

/* Write the decimal representation of VALUE to STREAM, padded on one side
   with spaces to be at least the absolute value of WIDTH long: if WIDTH >=
   0, then on the left side, otherwise on the right side.  If COUNT is
   non-NULL, the number of characters written is added to the integer it
   points to.  If an error occurs, the error code is returned, otherwise 0.  */
error_t
ps_write_int_field(int value, int width, FILE *stream, int *count)
{
  char buf[20];
  sprintf(buf, "%d", value);
  return ps_write_field(buf, width, stream, count);
}
