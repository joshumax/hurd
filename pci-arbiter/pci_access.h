/*
 * (C) Copyright IBM Corporation 2006
 * Copyright 2009 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * IBM AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Copyright (c) 2007 Paulo R. Zanoni, Tiago Vignatti
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/*
 * PCI access general header.
 *
 * Following code is borrowed from libpciaccess:
 * https://cgit.freedesktop.org/xorg/lib/libpciaccess/
 */

#ifndef PCI_ACCESS_H
#define PCI_ACCESS_H

#include <stddef.h>
#include <stdint.h>
#include <errno.h>

typedef uint64_t pciaddr_t;

/*
 * BAR descriptor for a PCI device.
 */
struct pci_mem_region
{
  /*
   * When the region is mapped, this is the pointer to the memory.
   */
  void *memory;

  /*
   * Base physical address of the region from the CPU's point of view.
   *
   * This address is typically passed to \c pci_device_map_range to create
   * a mapping of the region to the CPU's virtual address space.
   */
  pciaddr_t base_addr;


  /*
   * Size, in bytes, of the region.
   */
  pciaddr_t size;


  /*
   * Is the region I/O ports or memory?
   */
  unsigned is_IO:1;

  /*
   * Is the memory region prefetchable?
   *
   * \note
   * This can only be set if \c is_IO is not set.
   */
  unsigned is_prefetchable:1;


  /*
   * Is the memory at a 64-bit address?
   *
   * \note
   * This can only be set if \c is_IO is not set.
   */
  unsigned is_64:1;
};

/*
 * PCI device.
 *
 * Contains all of the information about a particular PCI device.
 */
struct pci_device
{
  /*
   * Complete bus identification, including domain, of the device.  On
   * platforms that do not support PCI domains (e.g., 32-bit x86 hardware),
   * the domain will always be zero.
   */
  uint16_t domain;
  uint8_t bus;
  uint8_t dev;
  uint8_t func;

  /*
   * Device's class, subclass, and programming interface packed into a
   * single 32-bit value.  The class is at bits [23:16], subclass is at
   * bits [15:8], and programming interface is at [7:0].
   */
  uint32_t device_class;

  /*
   * BAR descriptors for the device.
   */
  struct pci_mem_region regions[6];

  /*
   * Size, in bytes, of the device's expansion ROM.
   */
  pciaddr_t rom_size;

  /*
   * Physical address of the ROM
   */
  pciaddr_t rom_base;

  /*
   * Mapped ROM
   */
  void *rom_memory;

  /*
   * Size of the configuration space
   */
  size_t config_size;
};

typedef error_t (*pci_io_op_t) (unsigned bus, unsigned dev, unsigned func,
				pciaddr_t reg, void *data, unsigned size);

typedef error_t (*pci_refresh_dev_op_t) (struct pci_device * dev,
					 int num_region, int rom);

/* Global PCI data */
struct pci_system
{
  size_t num_devices;
  struct pci_device *devices;

  /* Callbacks */
  pci_io_op_t read;
  pci_io_op_t write;
  pci_refresh_dev_op_t device_refresh;
};

struct pci_system *pci_sys;

int pci_system_init (void);

#endif /* PCI_ACCESS_H */
