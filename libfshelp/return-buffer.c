/* Make a malloced buffer suitable for returning from a mach rpc

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   675 Mass Ave, Cambridge, MA 02139, USA. */

#include <string.h>
#include <mach.h>

#include "fshelp.h"

/* Puts data from the malloced buffer BUF, LEN bytes long, into RBUF & RLEN,
   suitable for returning from a mach rpc.  If LEN > 0, BUF is freed,
   regardless of whether an error is returned or not.  */
error_t
fshelp_return_malloced_buffer (char *buf, size_t len,
			       char **rbuf, mach_msg_type_number_t *rlen)
{
  error_t err = 0;

  if (*rlen < len)
    err = vm_allocate (mach_task_self (), (vm_address_t *)rbuf, len, 1);
  if (! err)
    {
      if (len)
	bcopy (buf, *rbuf, len);
      *rlen = len;
    }

  if (len > 0)
    free (buf);

  return err;
}
