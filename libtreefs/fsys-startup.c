/* fsys startup RPC

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

error_t
treefs_S_fsys_startup (mach_port_t child_boot_port, mach_port_t control_port,
		       mach_port_t *real, mach_msg_type_name_t *real_type)
{
  error_t err;
  struct port_info *child_boot =
    ports_check_port_type (child_boot_port, PT_TRANSBOOT);

  assert_backtrace (child_boot);			/* XXX deal with exec server boot */
  err = fshelp_handle_fsys_startup (child_boot, control_port, real, real_type);
  ports_done_with_port (child_boot);

  return err;
}
