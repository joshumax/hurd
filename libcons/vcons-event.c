/* vcons-event.c - Handle console events.
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

#include "cons.h"
#include "priv.h"

void
_cons_vcons_console_event (vcons_t vcons, int event)
{
  if (_cons_show_mouse & event)
    cons_vcons_set_mousecursor_status (vcons, 1);
  else if (_cons_hide_mouse & event)
    cons_vcons_set_mousecursor_status (vcons, 0);
}
