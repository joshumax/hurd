/* 
   Copyright (C) 1995 Free Software Foundation, Inc.

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

#ifndef __PSHOST_H__
#define __PSHOST_H__

#include <mach/mach_types.h>
#include <mach/host_info.h>

/* ---------------------------------------------------------------- */

/*
   The Basic & Sched info types are pretty static, so we cache them, but load
   info is dynamic so we don't cache that.

   See <mach/host_info.h> for information on the data types these routines
   return.
*/

/* Return the current host port.  */
host_t ps_get_host();

/* Return a pointer to basic info about the current host in HOST_INFO.  Since
   this is static global information we just use a static buffer.  If a
   system error occurs, the error code is returned, otherwise 0.  */
error_t ps_host_basic_info(host_basic_info_t *host_info);

/* Return a pointer to scheduling info about the current host in HOST_INFO.
   Since this is static global information we just use a static buffer.  If a
   system error occurs, the error code is returned, otherwise 0.  */
error_t ps_host_sched_info(host_sched_info_t *host_info);

/* Return a pointer to load info about the current host in HOST_INFO.  Since
   this is global information we just use a static buffer (if someone desires
   to keep old load info, they should copy the buffer we return a pointer
   to).  If a system error occurs, the error code is returned, otherwise 0.  */
error_t ps_host_load_info(host_load_info_t *host_info);

#endif /* __PSHOST_H__ */
