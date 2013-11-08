/* Demultiplexer for diskfs library
   Copyright (C) 1994, 1995, 1996 Free Software Foundation, Inc.

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

#include "priv.h"

int
diskfs_demuxer (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  int diskfs_fs_server (mach_msg_header_t *, mach_msg_header_t *);
  int diskfs_io_server (mach_msg_header_t *, mach_msg_header_t *);
  int diskfs_fsys_server (mach_msg_header_t *, mach_msg_header_t *);
  int diskfs_exec_startup_server (mach_msg_header_t *, mach_msg_header_t *);
  int diskfs_ifsock_server (mach_msg_header_t *, mach_msg_header_t *);
  int diskfs_startup_notify_server (mach_msg_header_t *, mach_msg_header_t *);
  
  
  return (diskfs_io_server (inp, outp)
	  || diskfs_fs_server (inp, outp)
	  || ports_notify_server (inp, outp)
	  || diskfs_fsys_server (inp, outp)
	  || diskfs_exec_startup_server (inp, outp)
	  || ports_interrupt_server (inp, outp)
	  || (diskfs_shortcut_ifsock ? diskfs_ifsock_server (inp, outp) : 0)
	  || diskfs_startup_notify_server (inp, outp));
}
