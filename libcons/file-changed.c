/* file-changed.c - Handling file changed notifications.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
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

#include <errno.h>
#include <assert-backtrace.h>
#include <pthread.h>

#include <mach.h>

#include "cons.h"
#include "fs_notify_S.h"

kern_return_t
cons_S_file_changed (cons_notify_t notify, natural_t tickno,
		     file_changed_type_t change,
		     off_t start, off_t end)
{
  error_t err = 0;
  vcons_t vcons = (vcons_t) notify;

  if (!notify || notify->cons)
    return EOPNOTSUPP;

  pthread_mutex_lock (&vcons->lock);
  switch (change)
    {
    case FILE_CHANGED_NULL:
      /* Always sent first for sync.  */
      cons_vcons_refresh (vcons);
      break;
    case FILE_CHANGED_WRITE:
      /* File data has been written.  */
      while (vcons->state.changes.written < vcons->display->changes.written)
	{
	  cons_change_t change;
	  
	  if (vcons->display->changes.written - vcons->state.changes.written
	      > vcons->cons->slack)
	    {
	      cons_vcons_refresh (vcons);
	      continue;
	    }
	  change = vcons->state.changes.buffer[vcons->state.changes.written
					       % vcons->state.changes.length];
	  if (vcons->display->changes.written - vcons->state.changes.written
	      > vcons->state.changes.length - 1)
	    {
	      /* While we were reading the entry, the server might
		 have overwritten it.  */
	      cons_vcons_refresh (vcons);
	      continue;
	    }
	  vcons->state.changes.written++;

	  if (change.what.not_matrix)
	    {
	      if (change.what.cursor_pos)
		{
		  uint32_t old_row = vcons->state.cursor.row;
		  uint32_t height = vcons->state.screen.height;
		  uint32_t row;

		  vcons->state.cursor.col = vcons->display->cursor.col;
		  row = vcons->state.cursor.row = vcons->display->cursor.row;

		  if (row + vcons->scrolling < height)
		    {
		      cons_vcons_set_cursor_pos (vcons,
						 vcons->state.cursor.col,
						 row + vcons->scrolling);
		      if (old_row + vcons->scrolling >= height)
			/* The cursor was invisible before.  */
			cons_vcons_set_cursor_status (vcons,
						      vcons->state.cursor.status);
		    }
		  else if (old_row + vcons->scrolling < height)
		    /* The cursor was visible before.  */
		    cons_vcons_set_cursor_status (vcons, CONS_CURSOR_INVISIBLE);

		  _cons_vcons_console_event (vcons, CONS_EVT_OUTPUT);
		  cons_vcons_update (vcons);
		}
	      if (change.what.cursor_status)
		{
		  vcons->state.cursor.status = vcons->display->cursor.status;
		  cons_vcons_set_cursor_status (vcons,
						vcons->state.cursor.status);
		  cons_vcons_update (vcons);
		}
	      if (change.what.screen_cur_line)
		{
		  uint32_t new_cur_line;

		  new_cur_line = vcons->display->screen.cur_line;

		  if (new_cur_line != vcons->state.screen.cur_line)
		    {
		      off_t size = vcons->state.screen.width
			* vcons->state.screen.lines;
		      off_t vis_start;
		      uint32_t scrolling;
		      off_t start;
		      off_t end;
		      
		      if (new_cur_line > vcons->state.screen.cur_line)
			scrolling = new_cur_line
			  - vcons->state.screen.cur_line;
		      else
			scrolling = UINT32_MAX - vcons->state.screen.cur_line
			  + 1 + new_cur_line;

		      /* If we are scrolling back, defer scrolling
			 until absolutely necessary.  */
		      if (vcons->scrolling)
			{
			  if (_cons_jump_down_on_output)
			    _cons_vcons_scrollback
			      (vcons, CONS_SCROLL_ABSOLUTE_LINE, 0);
			  else
			    {
			      if (vcons->scrolling + scrolling
				  <= vcons->state.screen.scr_lines)
				{
				  vcons->scrolling += scrolling;
				  scrolling = 0;
				}
			      else
				{
				  scrolling -= vcons->state.screen.scr_lines
				    - vcons->scrolling;
				  vcons->scrolling
				    = vcons->state.screen.scr_lines;
				}
			    }
			}

		      if (scrolling)
			{
			  uint32_t cur_disp_line;

			  if (new_cur_line >= vcons->scrolling)
			    cur_disp_line = new_cur_line - vcons->scrolling;
			  else
			    cur_disp_line = (UINT32_MAX - (vcons->scrolling - new_cur_line)) + 1;

			  if (scrolling > vcons->state.screen.height)
			    scrolling = vcons->state.screen.height;
			  if (scrolling < vcons->state.screen.height)
			    cons_vcons_scroll (vcons, scrolling);
			  else
			    cons_vcons_clear (vcons, vcons->state.screen.width
					      * vcons->state.screen.height,
					      0, 0);
			  vis_start = vcons->state.screen.width
			    * (cur_disp_line % vcons->state.screen.lines);
			  start = (((cur_disp_line % vcons->state.screen.lines)
				    + vcons->state.screen.height - scrolling)
				   * vcons->state.screen.width) % size;
			  end = start + scrolling * vcons->state.screen.width - 1;
			  cons_vcons_write (vcons,
					    vcons->state.screen.matrix + start,
					    end < size
					    ? end - start + 1 
					    : size - start,
					    0, vcons->state.screen.height
					    - scrolling);
			  if (end >= size)
			    cons_vcons_write (vcons,
					      vcons->state.screen.matrix,
					      end - size + 1,
					      0, (size - vis_start)
					      / vcons->state.screen.width);
			  _cons_vcons_console_event (vcons, CONS_EVT_OUTPUT);
			  cons_vcons_update (vcons);
			}
		      vcons->state.screen.cur_line = new_cur_line;
		    }
		}
	      if (change.what.screen_scr_lines)
		{
		  vcons->state.screen.scr_lines
		    = vcons->display->screen.scr_lines;
		  if (vcons->state.screen.scr_lines < vcons->scrolling)
		    assert_backtrace (!"Implement shrinking scrollback buffer! XXX");
		}
	      if (change.what.bell_audible)
		{
		  while (vcons->state.bell.audible
			 < vcons->display->bell.audible)
		    {
		      if (_cons_audible_bell == BELL_AUDIBLE)
			cons_vcons_beep (vcons);
		      else if (_cons_audible_bell == BELL_VISUAL)
			cons_vcons_flash (vcons);
		      vcons->state.bell.audible++;
		    }
		}
	      if (change.what.bell_visible)
		{
		  while (vcons->state.bell.visible
			 < vcons->display->bell.visible)
		    {
		      if (_cons_visual_bell == BELL_VISUAL)
			cons_vcons_flash (vcons);
		      else if (_cons_visual_bell == BELL_AUDIBLE)
			cons_vcons_beep (vcons);
		      vcons->state.bell.visible++;
		    }
		}
	      if (change.what.flags)
		{
		  uint32_t flags = vcons->display->flags;

		  if ((flags & CONS_FLAGS_SCROLL_LOCK)
		      != (vcons->state.flags & CONS_FLAGS_SCROLL_LOCK))
		    cons_vcons_set_scroll_lock (vcons, flags
						& CONS_FLAGS_SCROLL_LOCK);
		  vcons->state.flags = flags;
		}
	    }
	  else
	    {
	      /* For clipping.  */
	      off_t size = vcons->state.screen.width*vcons->state.screen.lines;
	      off_t rotate;
	      off_t vis_end = vcons->state.screen.height
		* vcons->state.screen.width - 1;
	      off_t end2 = -1;
	      off_t start_rel = 0;    /* start relative to visible start.  */
	      off_t start = change.matrix.start;
	      off_t end = change.matrix.end;

	      if (vcons->scrolling && _cons_jump_down_on_output)
		_cons_vcons_scrollback (vcons, CONS_SCROLL_ABSOLUTE_LINE, 0);

	      if (vcons->state.screen.cur_line >= vcons->scrolling)
		rotate = vcons->state.screen.cur_line - vcons->scrolling;
	      else
		rotate = (UINT32_MAX - (vcons->scrolling - vcons->state.screen.cur_line)) + 1;
	      rotate = vcons->state.screen.width * (rotate % vcons->state.screen.lines);

	      /* Rotate the buffer.  */
	      start -= rotate;
	      if (start < 0)
		start += size;
	      end -= rotate;
	      if (end < 0)
		end += size;
	      
	      /* Find the intersection.  */
	      if (start > vis_end)
		{
		  if (end < start)
		    {
		      start = 0;
		      if (vis_end < end)
			end = vis_end;
		    }
		  else
		    start = -1;
		}
	      else
		{
		  if (end >= start)
		    {
		      if (end > vis_end)
			end = vis_end;
		    }
		  else
		    {
		      end2 = end;
		      end = vis_end;
		    }
		}
	      /* We now have three cases: No intersection if start ==
		 -1, one intersection [start;end] if end2 == -1, and
		 two intersections [start;end] and [0;end2] if end2 !=
		 -1.  However, we still have to undo the buffer
		 rotation.  */
	      if (start != -1)
		{
		  start_rel = start;
		  start += rotate;
		  if (start >= size)
		    start -= size;
		  end += rotate;
		  if (end >= size)
		    end -= size;
		  if (start > end)
		    end += size;
		}
	      if (end2 != -1)
		/* The interval should be [vis_start:end2].  */
		end2 += rotate;
	      
	      if (start != -1)
		{
		  cons_vcons_clear (vcons, end - start + 1,
				    start_rel % vcons->state.screen.width,
				    start_rel / vcons->state.screen.width);
		  cons_vcons_write (vcons, vcons->state.screen.matrix + start,
				    end < size
				    ? end - start + 1
				    : size - start,
				    start_rel % vcons->state.screen.width,
				    start_rel / vcons->state.screen.width);
		  if (end >= size)
		    cons_vcons_write (vcons, vcons->state.screen.matrix,
				      end - size + 1,
				      (size - rotate)
				      % vcons->state.screen.width,
				      (size - rotate)
				      / vcons->state.screen.width);
		  if (end2 != -1)
		    {
		      cons_vcons_clear (vcons, end2 - rotate + 1, 0, 0);
		      cons_vcons_write (vcons,
					vcons->state.screen.matrix + rotate,
					end2 < size
					? end2 - rotate + 1
					: size - rotate,
					0, 0);
		      if (end2 >= size)
			cons_vcons_write (vcons, vcons->state.screen.matrix,
					  end2 - size + 1,
					  (size - rotate)
					  % vcons->state.screen.width,
					  (size - rotate)
					  / vcons->state.screen.width);
		    }
		  _cons_vcons_console_event (vcons, CONS_EVT_OUTPUT);
		  cons_vcons_update (vcons);
		}
	    }
	}
      break;
    case FILE_CHANGED_EXTEND:
      /* File has grown.  */
    case FILE_CHANGED_TRUNCATE:
      /* File has been truncated.  */
    case FILE_CHANGED_META:
      /* Stat information has changed, and none of the previous three
	 apply.  Not sent for changes in node times.  */
    default:
      err = EINVAL;
    };

  pthread_mutex_unlock (&vcons->lock);
  return err;
}
