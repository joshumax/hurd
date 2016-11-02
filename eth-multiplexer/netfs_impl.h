/*
   Copyright (C) 2008, 2009 Free Software Foundation, Inc.
   Written by Zheng Da.

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

#ifndef NETFS_IMPL
#define NETFS_IMPL

#include <hurd.h>
#include <mach.h>

#include "vdev.h"

struct netnode
{
  struct lnode *ln;
  char *name;
};

struct lnode
{
  struct vether_device vdev;
  struct stat st;
  struct node *n;
};

extern file_t root_file;
volatile struct mapped_time_value *multiplexer_maptime;

error_t new_node (struct lnode *ln, struct node **np);

#endif
