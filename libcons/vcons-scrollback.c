/* vcons-scrollback.c - Move forward and backward in the scrollback buffer.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <stdint.h>

#include <cthreads.h>

#include "cons.h"

/* Scroll back into the history of VCONS by DELTA lines.  */
int
cons_vcons_scrollback (vcons_t vcons, int delta)
{
  int scrolling;
  uint32_t new_scr;

  mutex_lock (&vcons->lock);
  if (delta > 0 || vcons->scrolling > (uint32_t) (-delta))
    {
      new_scr = vcons->scrolling + delta;
      if (new_scr > vcons->state.screen.scr_lines)
	new_scr = vcons->state.screen.scr_lines;
    }
  else
    new_scr = 0;

  if (new_scr == vcons->scrolling)
    {
      mutex_unlock (&vcons->lock);
      return 0;
    }
  
  scrolling = vcons->scrolling - new_scr;
  {
    uint32_t new_cur_line;
    off_t size = vcons->state.screen.width
      * vcons->state.screen.lines;
    off_t vis_start;
    off_t start;
    off_t end;

    if (vcons->state.screen.cur_line >= new_scr)
      new_cur_line = vcons->state.screen.cur_line - new_scr;
    else
      new_cur_line = (UINT32_MAX - (new_scr - vcons->state.screen.cur_line)) + 1;

    if (scrolling > 0 && (uint32_t) scrolling > vcons->state.screen.height)
      scrolling = vcons->state.screen.height;
    else if (scrolling < 0
	     && (uint32_t) (-scrolling) > vcons->state.screen.height)
      scrolling = -vcons->state.screen.height;
    if ((scrolling > 0 && scrolling < vcons->state.screen.height)
	|| (scrolling < 0
	    && (uint32_t) (-scrolling) < vcons->state.screen.height))
      cons_vcons_scroll (vcons, scrolling);

    vis_start = vcons->state.screen.width
      * (new_cur_line % vcons->state.screen.lines);
    if (scrolling > 0)
      start = (((new_cur_line % vcons->state.screen.lines)
		+ vcons->state.screen.height - scrolling)
	       * vcons->state.screen.width) % size;
    else
      start = vis_start;
    end = start + abs (scrolling) * vcons->state.screen.width - 1;

    cons_vcons_write (vcons,
		      vcons->state.screen.matrix + start,
		      end < size
		      ? end - start + 1 
		      : size - start,
		      0, (scrolling > 0)
		      ? vcons->state.screen.height - scrolling : 0);
    if (end >= size)
      cons_vcons_write (vcons,
			vcons->state.screen.matrix,
			end - size + 1,
			0, (size - vis_start)
			/ vcons->state.screen.width);
  }

  /* Set the new cursor position.  */
  {
    uint32_t row = vcons->state.cursor.row;
    uint32_t height = vcons->state.screen.height;

    if (row + new_scr < height)
      {
	cons_vcons_set_cursor_pos (vcons, vcons->state.cursor.col,
				   row + new_scr);
	if (row + vcons->scrolling >= height)
	  /* The cursor was invisible before.  */
	  cons_vcons_set_cursor_status (vcons, vcons->state.cursor.status);
      }
    else if (row + vcons->scrolling < height)
      /* The cursor was visible before.  */
      cons_vcons_set_cursor_status (vcons, CONS_CURSOR_INVISIBLE);
  }

  cons_vcons_update (vcons);
  vcons->scrolling -= scrolling;
  mutex_unlock (&vcons->lock);

  return -scrolling;
}
