/* Demultiplexer for ports library
   Copyright (C) 1994 Free Software Foundation

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
ports_demuxer (mach_msg_header_t *inp,
	       mach_msg_header_t *outp)
{
  int fs_server (mcch_msg_header_t *, mach_msg_header_t *);
  int io_server (mcch_msg_header_t *, mach_msg_header_t *);
  int fsys_server (mcch_msg_header_t *, mach_msg_header_t *);
  int seqnos_notify_server (mcch_msg_header_t *, mach_msg_header_t *);
  int exec_server (mcch_msg_header_t *, mach_msg_header_t *);
  
  return (io_server (inp, outp)
	  || pager_demuxer (inp, outp)
	  || fs_server (inp, outp)
	  || seqnos_notify_server (inp, outp)
	  || fsys_server (inp, outp)
	  || exec_server (inp, outp));
}

  
