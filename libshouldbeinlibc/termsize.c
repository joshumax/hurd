/* Function to try and deduce what size the terminal is

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

#include <sys/ioctl.h>

/* Returns what we think is the size of the terminal attached to
   file descriptor FD, of type TYPE, in WIDTH and/or HEIGHT.  If FD is
   negative, the terminal isn't queried, and if TYPE is NULL, it isn't used.
   Both WIDTH and HEIGHT may be NULL if only partial information is needed.
   True is returned upon success.  Even if false is returned, both output
   values are still written, with 0 for unknown, in case partial information
   is useful.  */
int
deduce_term_size (int fd, char *type, int *width, int *height)
{
  int w = 0, h = 0;
  struct winsize ws;
  
  if (fd >= 0 && ioctl (fd, TIOCGWINSZ, &ws) == 0)
    /* Look at the actual terminal.  */
    {
      w = ws.ws_col;
      h = ws.ws_row;
    }
  if (((width && !w) || (height && !h)) && type)
    /* Try the terminal type.  */
    {
      /* XXX */
    }

  if (width)
    *width = w;
  if (height)
    *height = h;

  return (!width || w) && (!height && h);
}
