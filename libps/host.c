/* Routines to get global host info.

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "ps.h"
#include "common.h"

/* ---------------------------------------------------------------- */

/*
   The Basic & Sched info types are pretty static, so we cache them, but load
   info is dynamic so we don't cache that.

   See <mach/host_info.h> for information on the data types these routines
   return.
*/

/* Return the current host port.  */
host_t 
ps_get_host ()
{
  static host_t host = MACH_PORT_NULL;
  if (host == MACH_PORT_NULL)
    host = mach_host_self ();
  return host;
}

/* Return a pointer to basic info about the current host in INFO.  Since
   this is static global information we just use a static buffer.  If a
   system error occurs, the error code is returned, otherwise 0.  */
error_t
ps_host_basic_info (host_basic_info_t *info)
{
  int initialized = FALSE;
  static host_basic_info_data_t buf;

  if (!initialized)
    {
      int size = sizeof (buf);
      error_t err = host_info (ps_get_host (), HOST_BASIC_INFO, 
			      (host_info_t) &buf, &size);
      if (err)
	return err;
      initialized = TRUE;
    }

  *info = &buf;
  return 0;
}

/* Return a pointer to scheduling info about the current host in INFO.
   Since this is static global information we just use a static buffer.  If a
   system error occurs, the error code is returned, otherwise 0.  */
error_t
ps_host_sched_info (host_sched_info_t *info)
{
  int initialized = FALSE;
  static host_sched_info_data_t buf;

  if (!initialized)
    {
      int size = sizeof (buf);
      error_t err = host_info (ps_get_host (), HOST_SCHED_INFO, 
			      (host_info_t) &buf, &size);
      if (err)
	return err;
      initialized = TRUE;
    }

  *info = &buf;
  return 0;
}

/* Return a pointer to load info about the current host in INFO.  Since
   this is global information we just use a static buffer (if someone desires
   to keep old load info, they should copy the buffer we return a pointer
   to).  If a system error occurs, the error code is returned, otherwise 0.  */
error_t
ps_host_load_info (host_load_info_t *info)
{
  static host_load_info_data_t buf;
  int size = sizeof (buf);
  error_t err = host_info (ps_get_host (), HOST_LOAD_INFO,
			  (host_info_t) &buf, &size);

  if (err)
    return err;

  *info = &buf;
  return 0;
}
