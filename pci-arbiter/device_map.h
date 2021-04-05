/*
   Copyright (C) 2021 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Header for device memory mapping functions */

#ifndef DEVICE_MAP_H
#define DEVICE_MAP_H

#include <hurd.h>

#include <pciaccess.h>

error_t device_map_region (struct pci_device *device,
			   struct pci_mem_region *region);

#endif /* DEVICE_MAP_H */
