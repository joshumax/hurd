/* vcons-input.c - Add input to a virtual console.
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

#include <errno.h>
#include <unistd.h>

#include "cons.h"
#include "priv.h"

/* Enter SIZE bytes from the buffer BUF into the virtual console
   VCONS.  */
error_t
cons_vcons_input (vcons_t vcons, char *buf, size_t size)
{
  int ret;

  mutex_lock (&vcons->lock);

  if (vcons->scrolling && _cons_jump_down_on_input)
    _cons_vcons_scrollback (vcons, CONS_SCROLL_ABSOLUTE_LINE, 0);

  do
    {
      ret = write (vcons->input, buf, size);
      if (ret > 0)
	{
	  size -= ret;
	  buf += ret;
	}
    }
  while (size && (ret != -1 || errno == EINTR));

  mutex_unlock (&vcons->lock);
  return 0;
}


