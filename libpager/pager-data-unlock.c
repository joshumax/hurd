/* Mark pages as writable.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Neal H Walfield

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

void
pager_data_unlock (struct pager *pager,
		   off_t start, off_t count)
{
  mutex_lock (&pager->interlock);
  _pager_lock_object (pager, start, count,
		     MEMORY_OBJECT_RETURN_NONE, 0,
		     VM_PROT_NONE, 0);
  mutex_unlock (&pager->interlock);
}
