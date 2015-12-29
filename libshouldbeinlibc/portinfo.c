/* Print information about a task's ports

   Copyright (C) 1996,98,99,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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

/* Prints info about NAME in TASK to STREAM, in a way described by the flags
   in SHOW.  If TYPE is non-zero, it should be what mach_port_type returns
   for NAME.  */
error_t
print_port_info (mach_port_t name, mach_port_type_t type, task_t task,
		 unsigned show, FILE *stream)
{
  int hex_names = (show & PORTINFO_HEX_NAMES);
  int first = 1;
  void comma ()
    {
      if (first)
	first = 0;
      else
	fprintf (stream, ", ");
    }
  void prefs (mach_port_right_t right)
    {
      mach_port_urefs_t refs;
      error_t err = mach_port_get_refs (task, name, right, &refs);
      if (! err)
	fprintf (stream, " (refs: %zu)", refs);
    }

  if (type == 0)
    {
      error_t err = mach_port_type (task, name, &type);
      if (err)
	return err;
    }

  fprintf (stream, hex_names ? "%#6lx: " : "%6lu: ", name);

  if (type & MACH_PORT_TYPE_RECEIVE)
    {
      comma ();
      fprintf (stream, "receive");
      if (show & PORTINFO_DETAILS)
	{
	  struct mach_port_status status;
	  error_t err = mach_port_get_receive_status (task, name, &status);
	  if (! err)
	    {
	      fprintf (stream, " (");
	      if (status.mps_pset != MACH_PORT_NULL)
		fprintf (stream,
			 hex_names ? "port-set: %#lx, " : "port-set: %lu, ",
			 status.mps_pset);
	      fprintf (stream, "seqno: %zu", status.mps_seqno);
	      if (status.mps_mscount)
		fprintf (stream, ", ms-count: %zu", status.mps_mscount);
	      if (status.mps_qlimit != MACH_PORT_QLIMIT_DEFAULT)
		fprintf (stream, ", qlimit: %zu", status.mps_qlimit);
	      if (status.mps_msgcount)
		fprintf (stream, ", msgs: %zu", status.mps_msgcount);
	      fprintf (stream, "%s%s%s)",
		       status.mps_srights ? ", send-rights" : "",
		       status.mps_pdrequest ? ", pd-req" : "",
		       status.mps_nsrequest ? ", ns-req" : "");
	    }
	}
    }
  if (type & MACH_PORT_TYPE_SEND)
    {
      comma ();
      fprintf (stream, "send");
      if (show & PORTINFO_DETAILS)
	prefs (MACH_PORT_RIGHT_SEND);
    }
  if (type & MACH_PORT_TYPE_SEND_ONCE)
    {
      comma ();
      fprintf (stream, "send-once");
    }
  if (type & MACH_PORT_TYPE_DEAD_NAME)
    {
      comma ();
      fprintf (stream, "dead-name");
      if (show & PORTINFO_DETAILS)
	prefs (MACH_PORT_RIGHT_DEAD_NAME);
    }
  if (type & MACH_PORT_TYPE_PORT_SET)
    {
      comma ();
      fprintf (stream, "port-set");
      if (show & PORTINFO_DETAILS)
	{
	  mach_port_t *members = 0;
	  mach_msg_type_number_t members_len = 0, i;
	  error_t err =
	    mach_port_get_set_status (task, name, &members, &members_len);
	  if (! err)
	    {
	      if (members_len == 0)
		fprintf (stream, " (empty)");
	      else
		{
		  fprintf (stream, hex_names ? " (%#lx" : " (%lu", members[0]);
		  for (i = 1; i < members_len; i++)
		    fprintf (stream, hex_names ? ", %#lx" : ", %lu",
			     members[i]);
		  fprintf (stream, ")");
		  munmap ((caddr_t) members, members_len * sizeof *members);
		}
	    }
	  }
    }
  putc ('\n', stream);

  return 0;
}

/* Prints info about every port in TASK that has a type in ONLY to STREAM. */
error_t
print_task_ports_info (task_t task, mach_port_type_t only,
		       unsigned show, FILE *stream)
{
  mach_port_t *names = 0;
  mach_port_type_t *types = 0;
  mach_msg_type_number_t names_len = 0, types_len = 0, i;
  error_t err = mach_port_names (task, &names, &names_len, &types, &types_len);

  if (err)
    return err;

  for (i = 0; i < names_len; i++)
    if (types[i] & only)
      print_port_info (names[i], types[i], task, show, stream);

  munmap ((caddr_t) names, names_len * sizeof *names);
  munmap ((caddr_t) types, types_len * sizeof *types);

  return 0;
}
