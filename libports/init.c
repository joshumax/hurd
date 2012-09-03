/* 
   Copyright (C) 1995, 2001 Free Software Foundation, Inc.
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

#include "ports.h"

pthread_mutex_t _ports_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t _ports_block = PTHREAD_COND_INITIALIZER;
struct port_bucket *_ports_all_buckets;
int _ports_total_rpcs;
int _ports_flags;
