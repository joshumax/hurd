/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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

error_t
netfs_S_io_read (struct protid *user,
		 char **data,
		 mach_msg_type_number_t *datalen,
		 off_t offset,
		 mach_msg_type_number_t amount)
{
  error_t err;
  int alloced = 0;

  if (!user)
    return EOPNOTSUPP;

  if (amount > *datalen)
    {
      alloced = 1;
      vm_allocate (mach_task_self (), (vm_address_t *) data, amount, 1);
    }
  *datalen = amount;

  err = netfs_attempt_read (user->credential, user->po->np, 
			    offset, datalen, *data);
  if (err && alloced)
    vm_deallocate (mach_task_self (), (vm_address_t) *data, amount);
  return err;
}

