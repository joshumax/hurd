/*
   Copyright (C) 2016-2019 Free Software Foundation, Inc.
   Written by Svante Signell <svante.signell@gmail.com>

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "priv.h"

static struct trivfs_node *
init_node (struct trivfs_node *tp)
{
  return NULL;
}

struct trivfs_node *
trivfs_make_node (struct trivfs_peropen *po)
{
  return NULL;
}
