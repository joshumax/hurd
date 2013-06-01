/* vcons-move-mouse.c - Catch mouse events.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Written by Marco Gerards.
   
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
#include <unistd.h>
#include <pthread.h>

#include "cons.h"
#include "priv.h"

static float mousepos_x;
static float mousepos_y;

error_t
cons_vcons_move_mouse (vcons_t vcons, mouse_event_t ev)
{
  char event[CONS_MOUSE_EVENT_LENGTH];
  uint32_t report_events;
  
  pthread_mutex_lock (&vcons->lock);
  report_events = vcons->display->flags & CONS_FLAGS_TRACK_MOUSE;
  
  switch (ev->mouse_movement)
    {
    case CONS_VCONS_MOUSE_MOVE_REL:
      mousepos_x += ((float) ev->x / _cons_mouse_sens);
      mousepos_y += ((float) ev->y / _cons_mouse_sens);
      break;

    case CONS_VCONS_MOUSE_MOVE_ABS_PERCENT:
      mousepos_x = vcons->state.screen.width * ev->x / 100;
      mousepos_y = vcons->state.screen.height * ev->y / 100;
      break;

    case CONS_VCONS_MOUSE_MOVE_ABS:
      mousepos_x = ev->x;
      mousepos_y = ev->y;
      break;
    }

  /* Keep the mouse cursor in range of the VC.  */
  if (mousepos_x < 0)
    mousepos_x = 0;
  if (mousepos_y < 0)
    mousepos_y = 0;
  if (mousepos_x >= (float) vcons->state.screen.width)
    mousepos_x = vcons->state.screen.width - 1;
  if (mousepos_y >= (float) vcons->state.screen.height)
    mousepos_y = vcons->state.screen.height - 1;
  
  cons_vcons_set_mousecursor_pos (vcons, (float) mousepos_x, (float) mousepos_y);
  
  /* Report a mouse movement event.  */
  if (ev->x || ev->y)
    _cons_vcons_console_event (vcons, CONS_EVT_MOUSE_MOVE);
  
  /* Report a mouse button event.  */
  if (ev->mouse_button != CONS_VCONS_MOUSE_BUTTON_NO_OP)
    _cons_vcons_console_event (vcons, CONS_EVT_MOUSE_BUTTON);
  
  if (report_events)
    {
      switch (ev->mouse_button)
	{
	case CONS_VCONS_MOUSE_BUTTON_NO_OP:
	  break;

	case CONS_VCONS_MOUSE_BUTTON_PRESSED:
	  /* Make an xterm like event string.  */
	  CONS_MOUSE_EVENT (event, ev->button, (int) mousepos_x + 1, (int) mousepos_y + 1);

	  _cons_vcons_input (vcons, event, CONS_MOUSE_EVENT_LENGTH);
	  /* And send it to the server.  */
	  break;

	case CONS_VCONS_MOUSE_BUTTON_RELEASED:
	  /* Make an xterm like event string.  */
	  CONS_MOUSE_EVENT (event, CONS_MOUSE_RELEASE, (int) mousepos_x + 1, (int) mousepos_y + 1);

	  /* And send it to the server.  */
	  _cons_vcons_input (vcons,  event, CONS_MOUSE_EVENT_LENGTH);
	  break;
	}
    }
  
  pthread_mutex_unlock (&vcons->lock);
  return 0;
}
