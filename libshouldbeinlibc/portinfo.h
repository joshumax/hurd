/* Print information about a task's ports

   Copyright (C) 1996, 1999 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#ifndef __PORTINFO_H__
#define __PORTINFO_H__

#include <stdio.h>
#include <errno.h>
#include <mach.h>

#include <portxlate.h>

/* Flags describing what to show.  */
#define PORTINFO_DETAILS	0x1
#define PORTINFO_MEMBERS	0x4
#define PORTINFO_HEX_NAMES	0x8

/* Prints info about NAME in TASK to STREAM, in a way described by the flags
   in SHOW.  If TYPE is non-zero, it should be what mach_port_type returns
   for NAME.  */
error_t print_port_info (mach_port_t name, mach_port_type_t type, task_t task,
			 unsigned show, FILE *stream);

/* Prints info about every port in TASK that has a type in ONLY to STREAM. */
error_t print_task_ports_info (task_t task, mach_port_type_t only,
			       unsigned show, FILE *stream);

/* Prints info about NAME translated through X to STREAM, in a way described
   by the flags in SHOW.  If TYPE is non-zero, it should be what
   mach_port_type returns for NAME in X->to_task.  */
error_t print_xlated_port_info (mach_port_t name, mach_port_type_t type,
				struct port_name_xlator *x,
				unsigned show, FILE *stream);

/* Prints info about every port common to both tasks in X, but only if the
   port in X->from_task has a type in ONLY, to STREAM.  */
error_t print_xlated_task_ports_info (struct port_name_xlator *x,
				      mach_port_type_t only,
				      unsigned show, FILE *stream);

#endif /* __PORTINFO_H__ */
