/* file-changed.c - Handling file changed notifications.
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

#include <mach.h>
#include <errno.h>

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

  mutex_lock (&vcons->lock);
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
		  vcons->state.cursor.col = vcons->display->cursor.col;
		  vcons->state.cursor.row = vcons->display->cursor.row;
		  cons_vcons_set_cursor_pos (vcons, vcons->state.cursor.col,
					     vcons->state.cursor.row);
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
		      if (scrolling > vcons->state.screen.height)
			scrolling = vcons->state.screen.height;
		      if (scrolling < vcons->state.screen.height)
			cons_vcons_scroll (vcons, scrolling);
		      vis_start = vcons->state.screen.width
			* (new_cur_line % vcons->state.screen.lines);
		      start = (((new_cur_line % vcons->state.screen.lines)
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
		      cons_vcons_update (vcons);
		      vcons->state.screen.cur_line = new_cur_line;
		    }
		}
	      if (change.what.screen_scr_lines)
		{
		  vcons->state.screen.scr_lines
		    = vcons->display->screen.scr_lines;
		}
	      if (change.what.bell_audible)
		{
		  while (vcons->state.bell.audible
			 < vcons->display->bell.audible)
		    {
		      cons_vcons_beep (vcons);
		      vcons->state.bell.audible++;
		    }
		}
	      if (change.what.bell_visible)
		{
		  while (vcons->state.bell.visible
			 < vcons->display->bell.visible)
		    {
		      cons_vcons_flash (vcons);
		      vcons->state.bell.visible++;
		    }
		}
	    }
	  else
	    {
	      /* For clipping.  */
	      off_t size = vcons->state.screen.width*vcons->state.screen.lines;
	      off_t rotate = vcons->state.screen.width
		* (vcons->state.screen.cur_line % vcons->state.screen.lines);
	      off_t vis_end = vcons->state.screen.height
		* vcons->state.screen.width - 1;
	      off_t end2 = -1;
	      off_t start_rel = 0;    /* start relative to visible start.  */
	      off_t start = change.matrix.start;
	      off_t end = change.matrix.end;

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

  mutex_unlock (&vcons->lock);
  return err;
}
