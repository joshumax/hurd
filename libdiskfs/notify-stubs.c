/* 
   Copyright (C) 1994, 1995 Free Software Foundation

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

error_t
diskfs_do_seqnos_mach_notify_port_deleted (mach_port_t notify 
					     __attribute__ ((unused)),
					   mach_port_seqno_t seqno
					     __attribute__ ((unused)),
					   mach_port_t name
					   __attribute__ ((unused)))
{
  return 0;
}

error_t
diskfs_do_seqnos_mach_notify_msg_accepted (mach_port_t notify
					     __attribute__ ((unused)),
					   mach_port_seqno_t seqno
					     __attribute__ ((unused)),
					   mach_port_t name
					     __attribute__ ((unused)))
{
  return 0;
}

error_t
diskfs_do_seqnos_mach_notify_port_destroyed (mach_port_t notify
					       __attribute__ ((unused)),
					     mach_port_seqno_t seqno
					       __attribute__ ((unused)),
					     mach_port_t name
					       __attribute__ ((unused)))
{
  return 0;
}

error_t
diskfs_do_seqnos_mach_notify_send_once (mach_port_t notify 
					  __attribute__ ((unused)),
					mach_port_seqno_t seqno
					  __attribute__ ((unused)))
{
  return 0;
}

error_t
diskfs_do_seqnos_mach_notify_dead_name (mach_port_t notify 
					  __attribute__ ((unused)),
					mach_port_seqno_t seqno
					  __attribute__ ((unused)),
					mach_port_t name
					  __atribute__ ((unused)))
{
  return 0;
}
