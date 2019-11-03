/*
   Copyright (C) 2017 Free Software Foundation, Inc.

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

/* Per-function files header */

#ifndef FUNC_FILES_H
#define FUNC_FILES_H

#include "pcifs.h"
#include <pciaccess.h>

typedef int (*pci_io_op_t) (struct pci_device *dev, void *data,
                 pciaddr_t reg, pciaddr_t width, pciaddr_t *bytes);

/* Config */
#define FILE_CONFIG_NAME  "config"

/* Rom */
#define FILE_ROM_NAME     "rom"

/* Region */
#define FILE_REGION_NAME     "region"

error_t io_config_file (struct pci_device * dev, off_t offset, size_t * len,
			void *data, pci_io_op_t op);

error_t read_rom_file (struct pci_device *dev, off_t offset, size_t * len,
		       void *data);

error_t io_region_file (struct pcifs_dirent *e, off_t offset, size_t * len,
			void *data, int read);
#endif /* FUNC_FILES_H */
