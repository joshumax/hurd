/* List of standard store classes

   Copyright (C) 1996, 1997 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include "store.h"

const struct store_class *const
store_std_classes[] = 
{
  &store_device_class, &store_file_class, &store_zero_class, &store_task_class,
  &store_ileave_class, &store_concat_class, &store_remap_class,
  &store_query_class, &store_copy_class, &store_gunzip_class,
  &store_typed_open_class,
  0
};

