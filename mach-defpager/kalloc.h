/* Backing store access callbacks for Hurd version of Mach default pager.
   Copyright (C) 2012 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/*
 * General kernel memory allocator.
 */

#ifndef _KALLOC_H_
#define _KALLOC_H_

void *kalloc (vm_size_t size);
void kfree (void *data, vm_size_t size);

#endif /* _KALLOC_H_ */
