/* List of standard store classes

   Copyright (C) 1996,97,2001 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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

const struct store_class *const __attribute__ ((section ("store_std_classes")))
store_std_classes[] =
{
  &store_device_class,
#if HAVE_PARTED_PARTED_H
  &store_part_class,
#endif
  &store_file_class,
  &store_zero_class,
  &store_task_class,
  &store_ileave_class, &store_concat_class, &store_remap_class,
  &store_query_class,
  &store_copy_class, &store_gunzip_class, &store_bunzip2_class,

  /* This pseudo-class must appear before any real STORAGE_NETWORK class,
     to parse STORAGE_NETWORK file_get_storage_info results properly.  */
  &store_url_open_class,
  &store_nbd_class,

  &store_typed_open_class,
  0
};
