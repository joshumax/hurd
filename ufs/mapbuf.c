/* Cache mappings of the disk
   Copyright (C) 1994 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */


#include "ufs.h"

struct mapbuf *mblist;
spin_lock_t mblistlock = SPIN_LOCK_INITIALIZER;

struct mapbuf *
map_region (vm_offset_t diskloc, vm_size_t length)
{
  struct mapbuf *mb;
  
  /* Check to see if we are already mapping this region */
  spin_lock (&mblistlock);
  for (mb = mblist; mb; mb = mb->next)
    {

