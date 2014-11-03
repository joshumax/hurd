/* Default hook for change to/from read-only

   Copyright (C) 2001 Free Software Foundation, Inc.

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

#include "diskfs.h"

/* The user may define this function.  It is called when the disk has been
   changed from read-only to read-write mode or vice-versa.  READONLY is the
   new state (which is also reflected in DISKFS_READONLY).  This function is
   also called during initial startup if the filesystem is to be writable.  */
void __attribute__ ((weak))
diskfs_readonly_changed (int readonly)
{
  /* By default do nothing at all.  */
}
