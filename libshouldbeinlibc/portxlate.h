/* Translate mach port names between two tasks

   Copyright (C) 1996 Free Software Foundation, Inc.

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

#ifndef __PORTXLATE_H__
#define __PORTXLATE_H__

#include <errno.h>
#include <mach.h>

/* A data structure specifying two tasks, and info used to translate port
   names between them.  */
struct port_name_xlator
{
  /* The tasks between which we are translating port names.  */
  mach_port_t from_task;
  mach_port_t to_task;

  /* True if we're translating receive rights in FROM_TASK; otherwise, we're
     translating send rights.  */
  int from_is_receive;

  /* Arrays of port names and type masks from TO_TASK, fetched by
     mach_port_names.  These are vm_allocated.  */
  mach_port_t *to_names;
  mach_msg_type_number_t to_names_len;
  mach_port_type_t *to_types;
  mach_msg_type_number_t to_types_len;

  /* An array of rights in the current task to the ports in TO_NAMES/TO_TASK,
     or MACH_PORT_NULL, indicating that none has been fetched yet.
     This vector is malloced.  */
  mach_port_t *ports;
};

/* Return a new port name translator translating names between FROM_TASK and
   TO_TASK, in XLATOR, or an error.  */
error_t port_name_xlator_create (mach_port_t from_task, mach_port_t to_task,
				 struct port_name_xlator **xlator);

/* Free the port name translator X and any resources it holds.  */
void port_name_xlator_free (struct port_name_xlator *x);

/* Translate the port FROM between the tasks in X, returning the translated
   name in TO, and the types of TO in TO_TYPE, or an error.  If TYPE is
   non-zero, it should be what mach_port_type returns for FROM.  */
error_t port_name_xlator_xlate (struct port_name_xlator *x,
				mach_port_t from, mach_port_type_t from_type,
				mach_port_t *to, mach_port_type_t *to_type);

#endif /* __PORTXLATE_H__ */
