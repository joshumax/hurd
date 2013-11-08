/* vcons-scrollback.c - Move forward and backward in the scrollback buffer.
   Copyright (C) 2002, 2004, 2005 Free Software Foundation, Inc.
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

#include <pthread.h>

#include "cons.h"
#include "priv.h"

/* Non-locking version of cons_vcons_scrollback.  Does also not update
   the display.  */
int
_cons_vcons_scrollback (vcons_t vcons, cons_scroll_t type, float value)
{
  int scrolling;
  uint32_t new_scr;

  switch (type)
    {
    case CONS_SCROLL_DELTA_LINES:
      scrolling = vcons->scrolling + ((uint32_t) value);
      break;
    case CONS_SCROLL_DELTA_SCREENS:
      scrolling = vcons->scrolling
	+ ((uint32_t) (value * vcons->state.screen.height));
      break;
    case CONS_SCROLL_ABSOLUTE_LINE:
      scrolling = (uint32_t) value;
      break;
    case CONS_SCROLL_ABSOLUTE_PERCENTAGE:
      scrolling = (uint32_t) (value * vcons->state.screen.scr_lines);
      break;
    default:
      return 0;
    }

  if (scrolling < 0)
    new_scr = 0;
  else if (scrolling > vcons->state.screen.scr_lines)
    new_scr = vcons->state.screen.scr_lines;
  else
    new_scr = scrolling;

  if (new_scr == vcons->scrolling)
    return 0;

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
    else if ((scrolling > 0 && scrolling == vcons->state.screen.height)
	|| (scrolling < 0
	    && (uint32_t) (-scrolling) == vcons->state.screen.height))
      cons_vcons_clear (vcons, vcons->state.screen.width
			* vcons->state.screen.height, 0, 0);

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

  vcons->scrolling -= scrolling;

  return -scrolling;
}

/* Scroll back into the history of VCONS.  If TYPE is
   CONS_SCROLL_DELTA_LINES, scroll up or down by VALUE lines.  If TYPE
   is CONS_SCROLL_DELTA_SCREENS, scroll up or down by VALUE multiples
   of a screen height.  If TYPE is CONS_SCROLL_ABSOLUTE_LINE, scroll to
   line VALUE (where 0 is the lowest line).  If TYPE is
   CONS_SCROLL_ABSOLUTE_PERCENTAGE, scroll to the position determined
   by VALUE, where 0 is the bottom and 1 is the top.

   The function returns the number of lines actually scrolled up or
   down.  */
int
cons_vcons_scrollback (vcons_t vcons, cons_scroll_t type, float value)
{
  int ret;

  pthread_mutex_lock (&vcons->lock);
  ret = _cons_vcons_scrollback (vcons, type, value);
  _cons_vcons_console_event (vcons, CONS_EVT_OUTPUT);
  cons_vcons_update (vcons);
  pthread_mutex_unlock (&vcons->lock);
  return ret;
}
