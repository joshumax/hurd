/* 
   Copyright (C) 1994 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef TRIVFS_PRIV_H_INCLUDED

#include <mach.h>
#include <hurd.h>
#include <hurd/ports.h>
#include "trivfs.h"

/* For the sake of MiG. */
typedef struct trivfs_protid *trivfs_protid_t;
typedef struct trivfs_control *trivfs_control_t;

struct trivfs_protid *_trivfs_begin_using_protid (mach_port_t);
void _trivfs_end_using_protid (struct trivfs_protid *);
struct trivfs_control *_trivfs_begin_using_control (mach_port_t);
void _trivfs_end_using_control (struct trivfs_control *);

#define TRIVFS_PRIV_H_INCLUDED
#endif

