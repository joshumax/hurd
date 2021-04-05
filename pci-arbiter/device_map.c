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

/* Implementation for device memory mapping functions */

#include <pciaccess.h>

#include "device_map.h"

error_t
device_map_region (struct pci_device *device, struct pci_mem_region *region)
{
  error_t err = 0;

  if (region->memory == 0)
    {
      err = pci_device_map_range (device, region->base_addr, region->size,
				  PCI_DEV_MAP_FLAG_WRITABLE, &region->memory);
    }

  return err;
}
