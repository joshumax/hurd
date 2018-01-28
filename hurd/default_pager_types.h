/* C declarations for Hurd default pager interface
   Copyright (C) 2001 Free Software Foundation, Inc.

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef _DEFAULT_PAGER_TYPES_H
#define _DEFAULT_PAGER_TYPES_H

#include <mach/std_types.h>		/* For mach_port_t et al. */
#include <mach/machine/vm_types.h>	/* For vm_size_t.  */
#include <device/device_types.h>	/* For recnum_t.  */

typedef recnum_t *recnum_array_t;
typedef const recnum_t *const_recnum_array_t;
typedef vm_size_t *vm_size_array_t;
typedef const vm_size_t *const_vm_size_array_t;

#endif
