/* console.h -- Interfaces for the console server.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <hurd/netfs.h>
#include <rwlock.h>
#include <maptime.h>

/* Handy source of time.  */
volatile struct mapped_time_value *console_maptime;

/* A handle for a console device.  */
typedef struct cons *cons_t;

/* A handle for a virtual console device.  */
typedef struct vcons *vcons_t;

#endif	/* CONSOLE_H */
