/*
   Copyright (C) 1993, 1994 Free Software Foundation

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful, 
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include <sys/file.h>

#include "fshelp.h"

/* Initialize a lock box. */
void
fshelp_lock_init (struct lock_box *box)
{
  box->type = LOCK_UN;
  pthread_cond_init (&box->wait, NULL);
  box->waiting = 0;
  box->shcount = 0;
}
