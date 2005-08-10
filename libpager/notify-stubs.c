/* 
   Copyright (C) 1995, 2011 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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

#include "priv.h"
#include "notify_S.h"
#include <errno.h>

error_t
_pager_do_seqnos_mach_notify_port_deleted (mach_port_t notify 
					     __attribute__ ((unused)),
					   mach_port_seqno_t seqno
					     __attribute__ ((unused)),
					   mach_port_t name
					   __attribute__ ((unused)))
{
  _pager_stubs_update_seqno (notify, seqno);

  return 0;
}

error_t
_pager_do_seqnos_mach_notify_msg_accepted (mach_port_t notify
					     __attribute__ ((unused)),
					   mach_port_seqno_t seqno
					     __attribute__ ((unused)),
					   mach_port_t name
					     __attribute__ ((unused)))
{
  _pager_stubs_update_seqno (notify, seqno);

  return 0;
}

error_t
_pager_do_seqnos_mach_notify_port_destroyed (mach_port_t notify
					       __attribute__ ((unused)),
					     mach_port_seqno_t seqno
					       __attribute__ ((unused)),
					     mach_port_t name
					       __attribute__ ((unused)))
{
  _pager_stubs_update_seqno (notify, seqno);

  return 0;
}

error_t
_pager_do_seqnos_mach_notify_send_once (mach_port_t notify 
					  __attribute__ ((unused)),
					mach_port_seqno_t seqno
					  __attribute__ ((unused)))
{
  _pager_stubs_update_seqno (notify, seqno);

  return 0;
}

error_t
_pager_do_seqnos_mach_notify_dead_name (mach_port_t notify 
					  __attribute__ ((unused)),
					mach_port_seqno_t seqno
					  __attribute__ ((unused)),
					mach_port_t name
					  __attribute__ ((unused)))
{
  _pager_stubs_update_seqno (notify, seqno);

  return 0;
}
