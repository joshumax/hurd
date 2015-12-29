/* Print information about a port, with the name translated between tasks

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

#include <sys/types.h>
#include <sys/mman.h>

#include "portinfo.h"

/* Prints info about NAME translated through X to STREAM, in a way described
   by the flags in SHOW.  If TYPE is non-zero, it should be what
   mach_port_type returns for NAME in X->to_task.  */
error_t
print_xlated_port_info (mach_port_t name, mach_port_type_t type,
			struct port_name_xlator *x,
			unsigned show, FILE *stream)
{
  mach_port_t old_name = name;
  error_t err = port_name_xlator_xlate (x, name, type, &name, &type);
  if (! err)
    {
      fprintf (stream, (show & PORTINFO_HEX_NAMES) ? "%#6lx => " : "%6lu => ",
	       old_name);
      err = print_port_info (name, type, x->to_task, show, stream);
    }
  return err;
}

/* Prints info about every port common to both tasks in X, but only if the
   port in X->from_task has a type in ONLY, to STREAM.  */
error_t
print_xlated_task_ports_info (struct port_name_xlator *x,
			      mach_port_type_t only,
			      unsigned show, FILE *stream)
{
  mach_port_t *names = 0;
  mach_port_type_t *types = 0;
  mach_msg_type_number_t names_len = 0, types_len = 0, i;
  error_t err =
    mach_port_names (x->from_task, &names, &names_len, &types, &types_len);

  if (err)
    return err;

  for (i = 0; i < names_len; i++)
    if (types[i] & only)
      print_xlated_port_info (names[i], types[i], x, show, stream);

  munmap ((caddr_t) names, names_len * sizeof *names);
  munmap ((caddr_t) types, types_len * sizeof *types);

  return 0;
}
