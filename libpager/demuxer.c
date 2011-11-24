/* Demuxer for pager library
   Copyright (C) 1994, 1995, 2002, 2011 Free Software Foundation

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
#include "memory_object_S.h"
#include "notify_S.h"

/* Demultiplex a single message directed at a pager port; INP is the
   message received; fill OUTP with the reply.  */
int
pager_demuxer (mach_msg_header_t *inp,
	       mach_msg_header_t *outp)
{
  extern int _pager_seqnos_memory_object_server (mach_msg_header_t *inp,
					  mach_msg_header_t *outp);
  extern int _pager_seqnos_notify_server (mach_msg_header_t *inp,
					  mach_msg_header_t *outp);

  int result = _pager_seqnos_memory_object_server (inp, outp)
    || _pager_seqnos_notify_server (inp, outp);
  if (!result)
    /* Synchronize our bookkeeping of the port's seqno with the one consumed by
       this bogus message.  */
    _pager_update_seqno (inp->msgh_local_port, inp->msgh_seqno);
  return result;
}
