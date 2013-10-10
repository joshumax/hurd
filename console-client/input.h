/* input.h - The interface to and for an input driver.
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

#ifndef _INPUT_H_
#define _INPUT_H_ 1

#include <errno.h>
#include <stddef.h>

#include <hurd/cons.h>


/* The input drivers are set up by the driver's initialization routine
   and added to the console client with driver_add_input.  All
   subsequent operations on the display are fully synchronized by the
   caller.  The driver deinitialization routine should call
   driver_remove_input.  */

/* Forward declaration.  */
struct input_ops;
typedef struct input_ops *input_ops_t;

/* Add the input source HANDLE with the operations OPS to the console
   client.  As soon as this is called, operations on this input source
   may be performed, even before the function returns.  */
error_t driver_add_input (input_ops_t ops, void *handle);

/* Remove the input HANDLE with the operations OPS from the console
   client.  As soon as this function returns, no operations will be
   performed on the input source anymore.  */
error_t driver_remove_input (input_ops_t ops, void *handle);

/* Enter SIZE bytes from the buffer BUF into the currently active
   console.  This can be called by the input driver at any time.  */
error_t console_input (char *buf, size_t size);

/* Scroll the active console by TYPE and VALUE as specified by
   cons_vcons_scrollback.  */
int console_scrollback (cons_scroll_t type, float value);

/* Returns current console ID.  */
error_t console_current_id (int *cur);

/* Switch the active console to console ID or DELTA (relative to the
   active console).  */
error_t console_switch (int id, int delta);

/* Signal an error to the user.  */
void console_error (const wchar_t *const err_msg);

/* Exit the console client.  Does not return.  */
void console_exit (void) __attribute__ ((noreturn));

/* Switch away from the console an external use of the console like
   XFree.  */
void console_switch_away (void);

/* Switch back to the console client from an external user of the
   console like XFree.  */
void console_switch_back (void);

/* Report the mouse event EV to the currently active console.  This
   can be called by the input driver at any time.  */
error_t console_move_mouse (mouse_event_t ev);


#if QUAERENDO_INVENIETIS
/* Do not use, do not remove.  */
void console_deprecated (int key);
#endif


struct input_ops
{
  /* Set the status of the scroll lock indication.  */
  error_t (*set_scroll_lock_status) (void *handle, int onoff);

  /* Do not use, do not remove.  */
  void (*deprecated) (void *handle, int key);
};

#endif	/* _INPUT_H_ */
