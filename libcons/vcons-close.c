/* vcons-close.c - Close a virtual console.
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
#include <stdio.h>
#include <sys/mman.h>
#include <sys/fcntl.h>

#include <hurd.h>
#include <mach.h>

#include "cons.h"

/* Close the virtual console VCONS.  VCONS->cons is locked.  */
void
cons_vcons_close (vcons_t vcons)
{
  if (vcons->input >= 0)
    {
      close (vcons->input);
      vcons->input = -1;
    }
  if (vcons->display != MAP_FAILED)
    {
      munmap (vcons->display, vcons->display_size);
      vcons->display = MAP_FAILED;
    }
  if (vcons->notify)
    {
      ports_destroy_right (vcons->notify);
      vcons->notify = NULL;
    }
}
