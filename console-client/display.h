/* display.h - The interface to and for a display driver.
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

#ifndef _DISPLAY_H_
#define _DISPLAY_H_ 1

#include <errno.h>
#include <stdint.h>

#include <hurd/console.h>


/* The display drivers are set up by the driver's initialization
   routine and added to the console client with driver_add_display.
   All subsequent operations on the display are fully synchronized by
   the caller.  The driver deinitialization routine should call
   driver_remove_display.  */

/* Forward declaration.  */
struct display_ops;
typedef struct display_ops *display_ops_t;

/* Add the display HANDLE with the operations OPS to the console
   client.  As soon as this is called, operations on this display may
   be performed, even before the function returns.  */
error_t driver_add_display (display_ops_t ops, void *handle);

/* Remove the display HANDLE with the operations OPS from the console
   client.  As soon as this function returns, no operations will be
   performed on the display anymore.  */
error_t driver_remove_display (display_ops_t ops, void *handle);


struct display_ops
{
  /* Set the cursor's position on display HANDLE to column COL and row
     ROW.  The home position (COL and ROW both being 0) is the upper
     left corner of the screen matrix.  COL will be smaller than the
     width and ROW will be smaller than the height of the display.

     The driver is allowed to delay the effect of this operation until
     the UPDATE function is called.  */
  error_t (*set_cursor_pos) (void *handle, uint32_t col, uint32_t row);


  /* Set the cursor's state to STATE on display HANDLE.  The possible
     values for STATE are defined by the CONS_CURSOR_* symbols in
     <hurd/console.h>.

     The driver is allowed to delay the effect of this operation until
     the UPDATE function is called.  */
  error_t (*set_cursor_status) (void *handle, uint32_t state);


  /* Scroll the display by DELTA lines up if DELTA is positive, and by
     -DELTA lines down if DELTA is negative.  DELTA will never be
     zero, and the absolute value if DELTA will be smaller than or
     equal to the height of the screen matrix.

     The area that becomes free will be filled in a subsequent write
     call.  The purpose of the function is two-fold: It is called with
     an absolute value of DELTA smaller than the screen height to
     perform scrolling.  It is called with an absolute value of DELTA
     equal to the screen height to prepare a full refresh of the
     screen.  In the latter case the driver should not really perform
     any scrolling.  Instead it might deallocate limited resources
     (like display glyph slots and palette colors) if that helps to
     perform the subsequent write.  It goes without saying that the
     same deallocation, if any, should be performed on the area that
     will be filled with the scrolled in content.

     XXX Possibly need a function to invalidate scrollback buffer, or
     in general to signal a switch of the console so state can be
     reset.  Only do this if we make guarantees about validity of
     scrollback buffer, of course.

     The driver is allowed to delay the effect of this operation until
     the UPDATE function is called.  */
  error_t (*scroll) (void *handle, int delta);

  /* Deallocate the scarce resources (like font glyph slots, colors
   etc) in the LENGTH entries of the screen matrix starting from
   position COL and ROW.  This call is immediately followed by calls
   to write which cover the same area.  If there are no scarce
   resources, the caller might do nothing.  */
  error_t (*clear) (void *handle, size_t length, uint32_t col, uint32_t row);

  /* Write the text STR with LENGTH characters to column COL and row
     ROW on display HANDLE.  LENGTH can be longer than the width of
     the screen matrix minus COL.  In this case the driver should
     automatically wrap around the edge.  However, LENGTH will never
     be so huge that the whole text starting from COL and ROW will go
     beyond the lower right corner of the screen matrix.

     The driver is allowed to delay the effect of this operation until
     the UPDATE function is called.  */
  error_t (*write) (void *handle, conchar_t *str, size_t length,
                    uint32_t col, uint32_t row);

  /* Flush all the past changes on display HANDLE that have not been
     flushed yet.  This should make all past changes visible to the
     user.  The driver is free to make them visible earlier, but it
     must happen before returning from this call.

     The purpose of this function is to group several related change
     operations together, but also several change operations which
     occur in rapid succession.  The first grouping is essential to
     make it possible for a driver to implement double buffering.  The
     second grouping is important to keep up performance if there is a
     lot of activity on the screen matrix.  */
  error_t (*update) (void *handle);

  /* Flash the display HANDLE once.  This should be done
     immediately.  */
  error_t (*flash) (void *handle);

  /* Do not use, do not remove.  */
  void (*deprecated) (void *handle, int key);

  /* Change the dimension of the physical screen to one that can
     display the vcons with the size of WIDTH * HEIGHT and clear the
     old screen.  If the physical screen already has the right
     resolution do nothing.  This function is always followed by a
     write that covers the whole new screen.  */
  error_t (*set_dimension) (void *handle, unsigned int width,
			    unsigned int height);

  /* Move the mouse cursor to the position X, Y.  If the mouse cursor
     is visible, update its position.  */
  error_t (*set_mousecursor_pos) (void *handle, float x, float y);

  /* If STATUS is set to 0, hide the mouse cursor, otherwise show
     it.  */
  error_t (*set_mousecursor_status) (void *handle, int status);
};

#endif	/* _DISPLAY_H_ */
