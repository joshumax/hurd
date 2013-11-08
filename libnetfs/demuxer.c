/* 
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "netfs.h"

int
netfs_demuxer (mach_msg_header_t *inp,
	       mach_msg_header_t *outp)
{
  int netfs_fs_server (mach_msg_header_t *, mach_msg_header_t *);
  int netfs_io_server (mach_msg_header_t *, mach_msg_header_t *);
  int netfs_fsys_server (mach_msg_header_t *, mach_msg_header_t *);
  int netfs_ifsock_server (mach_msg_header_t *, mach_msg_header_t *);

  return (netfs_io_server (inp, outp)
          || netfs_fs_server (inp, outp)
          || ports_notify_server (inp, outp)
          || netfs_fsys_server (inp, outp)
          || ports_interrupt_server (inp, outp)
          || netfs_ifsock_server (inp, outp));
}
