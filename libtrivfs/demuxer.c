/*
   Copyright (C) 1993, 1994 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"

int
trivfs_demuxer (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  int trivfs_fs_server (mach_msg_header_t *, mach_msg_header_t *);
  int trivfs_io_server (mach_msg_header_t *, mach_msg_header_t *);
  int trivfs_fsys_server (mach_msg_header_t *, mach_msg_header_t *);
  
  return (trivfs_io_server (inp, outp)
	  || trivfs_fs_server (inp, outp)
	  || ports_notify_server (inp, outp)
	  || trivfs_fsys_server (inp, outp)
	  || ports_interrupt_server (inp, outp));
}
