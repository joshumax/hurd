/* vcons-add.c - Add a virtual console.
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

#include "cons.h"

/* The virtual console VCONS was just added.  VCONS->cons is
   locked.  */
void
cons_vcons_add (vcons_t vcons)
{
  error_t err;

  /* The first console added will be activated automatically.  */
  if (vcons->cons->active)
    return;

  /* Forward the activation request to the user.  */
  err = cons_vcons_activate (vcons);
  if (!err)
    {
      vcons->cons->active = vcons;
      cons_vcons_refresh (vcons);
    }
}
