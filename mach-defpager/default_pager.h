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

/* Prototypes for working with paging partitions and files */

#ifndef _DEFAULT_PAGER_H_
#define _DEFAULT_PAGER_H_

#include <file_io.h>

void partition_init();

void create_paging_partition(const char *name, struct file_direct *fdp,
                             int isa_file, int linux_signature);
kern_return_t destroy_paging_partition(char *name, void **pp_private);

kern_return_t add_paging_file(mach_port_t master_device_port,
			      char *file_name, int linux_signature);
kern_return_t remove_paging_file (char *file_name);

void paging_space_info(vm_size_t *totp, vm_size_t *freep);
void no_paging_space(boolean_t out_of_memory);
void overcommitted(boolean_t got_more_space, vm_size_t space);

void panic (const char *fmt, ...);

#endif /* _DEFAULT_PAGER_H_ */
