/*
   Copyright (C) 1993, 1994, 2013 Free Software Foundation

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

#include "trivfs_io_S.h"
#include "trivfs_fs_S.h"
#include "../libports/notify_S.h"
#include "trivfs_fsys_S.h"
#include "../libports/interrupt_S.h"

int
trivfs_demuxer (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = trivfs_io_server_routine (inp)) ||
      (routine = trivfs_fs_server_routine (inp)) ||
      (routine = ports_notify_server_routine (inp)) ||
      (routine = trivfs_fsys_server_routine (inp)) ||
      (routine = ports_interrupt_server_routine (inp)))
    {
      (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}
