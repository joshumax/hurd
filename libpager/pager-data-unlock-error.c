/* Indicate an error unlocking some pages.
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
pager_data_unlock_error (struct pager *pager,
			 off_t start, off_t npages,
			 error_t err)
{
  mutex_lock (&pager->interlock);

  /* Flush the range and set a bit so that
     m_o_data_request knows to issue an error.  */
  _pager_lock_object (pager, start, npages,
		     MEMORY_OBJECT_RETURN_NONE, 1,
		     VM_PROT_WRITE, 1);

  _pager_pagemap_resize (pager, start + npages);
  _pager_mark_next_request_error (pager, start, npages, err);

  mutex_unlock (&pager->interlock);
}
