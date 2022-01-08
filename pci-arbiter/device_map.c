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
device_map_region (struct pci_device *device, struct pci_mem_region *region,
		   void **addr)
{
  error_t err = 0;

  if (*addr == 0)
    {
      /*
       * We could use the non-legacy call for all ranges, but libpciaccess
       * offers a call for ranges under 1Mb. We call it for those cases, even
       * when there's no difference for us.
       */
      if (region->base_addr > 0x100000
	  || region->base_addr + region->size > 0x100000)
        err = pci_device_map_range (device, region->base_addr, region->size,
				    PCI_DEV_MAP_FLAG_WRITABLE, addr);
      else
        err = pci_device_map_legacy (device, region->base_addr, region->size,
				    PCI_DEV_MAP_FLAG_WRITABLE, addr);
    }

  return err;
}
