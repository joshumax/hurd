/* Simple output formatting functions

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <malloc.h>
#include <string.h>
#include <ctype.h>

#include <line.h>

/* Return a new line structure, which will output to STREAM.  WIDTH is the
   maximum number of characters per line.  If enough memory can't be
   allocated, 0 is returned.  */
struct line *
make_line (FILE *stream, unsigned width)
{
  struct line *line = malloc (sizeof (struct line));
  if (line)
    {
      line->buf = malloc (width + 2);
      if (line->buf)
	{
	  line->max = line->buf + width;
	  line->point = line->buf;
	  line->stream = stream;
	}
      else
	{
	  free (line);
	  line = 0;
	}
    }
  return line;
}

/* Free LINE and any resources it uses.  */
void
line_free (struct line *line)
{
  if (line->point > line->buf)
    line_newline (line, 0);
  free (line->buf);
  free (line);
}

/* Adds the text in STR to LINE, wrapping words as necessary to fit.
   LMARGIN is the left margin used when wrapping; whitespace is deleted at
   wrap-points.  Newlines in STR are honoured by adding a newline and
   indenting to LMARGIN; any following whitespace is kept.  */
void
line_fill (struct line *line, char *str, unsigned lmargin)
{
  while (*str)
    {
      char *word_end = str;

      while (*word_end == ' ')
	word_end++;

      if (*word_end == '\n')
	{
	  if (line_column (line) > lmargin)
	    line_newline (line, lmargin);
	  str = word_end + 1;
	}
      else if (*word_end)
	{
	  char *word_start = word_end;
	  while (*word_end && !isspace (*word_end))
	    word_end++;
	  if (line_left (line, word_end - str) >= 0)
	    {
	      line_write (line, str, word_end - str);
	      str = word_end;
	    }
	  else
	    /* Word won't fit on the current line, move to the next one. */
	    {
	      line_newline (line, lmargin);
	      str = word_start; /* Omit spaces when wrapping.  */
	    }
	}
    }
}

/* Clean up after a printf to LINE, to take care of any newlines that might
   have been added.  ADDED is the amount the printf has added to the line.
   We take care of updating LINE's point.  */
void
_line_cleanup_printf (struct line *line, unsigned added)
{
  char *point = line->point, *new_point = point + added, *last_nl = new_point;

  while (last_nl > point)
    if (*--last_nl == '\n')
      /* There's a newline; deal.  */
      {
	last_nl++;
	fwrite (line->buf, 1, last_nl - line->buf, line->stream);
	if (last_nl < new_point)
	  bcopy (last_nl, line->buf, new_point - last_nl);
	new_point -= (last_nl - line->buf);
	break;
      }

  line->point = new_point;
}

/* Add STR, of length LEN, to LINE.  */
void
line_write (struct line *line, char *str, unsigned len)
{
  char *end = memchr (str, '\n', len) ?: str + len;
  unsigned line_len = end - str;
  char *p = line->point, *max = line->max;
  if (line_len > max - p)
    line_len = max - p;
  bcopy (str, p, line_len);
  p += line_len;
  if (line_len == len)
    line->point = p;
  else
    {
      char *buf = line->buf;
      fwrite (buf, 1, p - buf, line->stream);
      line->point = buf;
      line_write (line, end + 1, len - line_len - 1);
    }
}
