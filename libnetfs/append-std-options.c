/* Get standard netfs run-time options

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include "netfs.h"

/* *Appends* to ARGZ & ARGZ_LEN '\0'-separated options describing the standard
   netfs option state (note that unlike netfs_get_options, ARGZ & ARGZ_LEN
   must already have a sane value).  */
error_t
netfs_append_std_options (char **argz, unsigned *argz_len)
{
  return 0;
}
