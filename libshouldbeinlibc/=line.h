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

#ifndef __LINE_H__
#define __LINE_H__

#include <stdio.h>

struct line
{
  char *buf;
  char *point, *max;
  FILE *stream;
};

extern void
_line_cleanup_printf (struct line *line, unsigned added);

/* Printf FMT & ARGS to LINE.  */
/* XXX This implementation is kind of bogus because it pretends to deal with
   newlines in the output, but it just uses LINE's buffer for the output and
   anything past the end of the buffer will get chopped.  A terminating
   newline will work ok though.  */
#define line_printf(line, fmt, args...)					\
  ({ struct line *_line = (line);		 			\
     _line_cleanup_printf (_line,		 			\
       snprintf (_line->point, _line->max - _line->point, (fmt) , ##args)); \
   })

/* Returns the amount of free space in line after adding AMOUNT characters to
   it (which will be negative if this would overflow).  */
extern inline int
line_left (struct line *line, unsigned amount)
{
  return line->max - line->point - amount;
}

/* Return the column position of LINE's output point, which starts at 0.  */
extern inline unsigned
line_column (struct line *line)
{
  return line->point - line->buf;
}

/* Add enough spaces to LINE to move the point to column TARGET.  */


extern inline void
line_indent_to (struct line *line, int target)
{
  while (line->point < line->buf + target && line->point < line->max)
    *line->point++ = ' ';
}

/* Emit the current contents of LINE and a newline to its stream, and fill
   LINE with LMARGIN spaces.  */
extern inline void
line_newline (struct line *line, int lmargin)
{
  *line->point++ = '\n';
  *line->point = '\0';
  fputs (line->buf, line->stream);
  line->point = line->buf;
  if (lmargin)
    line_indent_to (line, lmargin);
}

/* If LINE isn't before or at column position LMARGIN, then add a newline
   and indent to that position.  */
extern inline void
line_freshline (struct line *line, int lmargin)
{
  if (line_column (line) > lmargin)
    line_newline (line, lmargin);
}

/* Add a character to LINE, unless it's full.  */
extern inline int
line_putc (struct line *line, int ch)
{
  if (ch == '\n')
    line_newline (line, 0);
  else if (line->point < line->max)
    *line->point++ = ch;
  return ch;
}

/* Adds the text in STR to LINE, wrapping words as necessary to fit.  LMARGIN
   is the left margin used when wrapping.  */
void line_fill (struct line *line, char *str, unsigned lmargin);

/* Add STR, of length LEN, to LINE.  */
void line_write (struct line *line, char *str, unsigned len);

/* Add STR to LINE.  */
extern inline void line_puts (struct line *line, char *str)
{
  line_write (line, str, strlen (str));
}

/* Return a new line structure, which will output to STREAM.  WIDTH is the
   maximum number of characters per line.  If enough memory can't be
   allocated, 0 is returned.  */
struct line *make_line (FILE *stream, unsigned width);

/* Free LINE and any resources it uses.  */
void line_free (struct line *line);

#endif /* __LINE_H__ */
