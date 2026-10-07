/*
   Copyright (C) 2018 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <<a rel="nofollow" href="http://www.gnu.org/licenses/">http://www.gnu.org/licenses/</a>>.
*/

#include <sys/mman.h>
#include <sys/io.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "myacpi.h"

#define __KERNEL__
#include <acpi/acpi.h>

static int
acpi_get_rsdp(struct rsdp_descr2 *rsdp)
{
  acpi_physical_address rsdp_phys = acpi_os_get_root_pointer();

  if (!rsdp_phys)
    return ENODEV;

  acpi_size map_sz = sizeof (struct rsdp_descr2);
  void *virt_addr = acpi_os_map_memory (rsdp_phys, map_sz);

  if (virt_addr == MAP_FAILED)
    {
      /* There is an extremely unlikely case that rsdp_phys is very
	 close to the maximum value possible and ACPI 1 is in use
	 which meant we were mapping more than necessary. */

      map_sz = sizeof (struct rsdp_descr);
      virt_addr = acpi_os_map_memory (rsdp_phys, map_sz);
    }

  if (virt_addr == MAP_FAILED)
    return errno;

  memcpy (rsdp, virt_addr, map_sz);
  acpi_os_unmap_memory (virt_addr, map_sz);

  if (memcmp (rsdp->v1.magic, RSDP_MAGIC, sizeof (rsdp->v1.magic)))
    return ENODEV;

  return 0;
}

int
acpi_get_num_tables(size_t *num_tables)
{
  struct rsdp_descr2 rsdp = { 0 };
  uintptr_t sdt_base = (uintptr_t)0;
  bool is_64bit = false;
  struct acpi_header *root_sdt;
  struct acpi_header *next;

  int err = acpi_get_rsdp (&rsdp);
  if (err)
    return err;

  if (rsdp.v1.revision == 0) {
    // ACPI 1.0
    sdt_base = rsdp.v1.rsdt_addr;
    is_64bit = false;
  } else if (rsdp.v1.revision == 2) {
    // ACPI >= 2.0
    sdt_base = rsdp.xsdt_addr;
    is_64bit = true;
  } else {
    return ENODEV;
  }

  /* Now we have the sdt_base address and knowledge of 32/64 bit ACPI */

  root_sdt = acpi_os_map_memory(sdt_base, ESCD_SIZE);
  if (root_sdt == MAP_FAILED)
    return errno;

  /* Get total tables */
  uint32_t ntables;
  uint8_t sz_ptr;
  sz_ptr = is_64bit ? 8 : 4;
  ntables = (root_sdt->length - sizeof(*root_sdt)) / sz_ptr;

  /* Get pointer to first ACPI table */
  void *acpi_ptr = (void*)root_sdt + sizeof(*root_sdt);

  /* Get number of readable tables */
  *num_tables = 0;
  for (int i = 0; i < ntables; i++)
    {
      if (is_64bit) {
        uint64_t acpi_ptr64;
        memcpy(&acpi_ptr64, acpi_ptr + i*sz_ptr, sizeof(acpi_ptr64));
        next = acpi_os_map_memory(acpi_ptr64, ESCD_SIZE);
      } else {
        uint32_t acpi_ptr32;
        memcpy(&acpi_ptr32, acpi_ptr + i*sz_ptr, sizeof(acpi_ptr32));
        next = acpi_os_map_memory(acpi_ptr32, ESCD_SIZE);
      }

      if (next == MAP_FAILED)
        return errno;

      if (next->signature[0] == '\0' || next->length == 0) {
        acpi_os_unmap_memory(next, ESCD_SIZE);
        continue;
      }
      *num_tables += 1;
      acpi_os_unmap_memory(next, ESCD_SIZE);
    }

  acpi_os_unmap_memory(root_sdt, ESCD_SIZE);

  return 0;
}

int
acpi_get_tables(struct acpi_table **tables)
{
  int err;
  struct rsdp_descr2 rsdp = { 0 };
  uintptr_t sdt_base = (uintptr_t)0;
  bool is_64bit = false;
  struct acpi_header *root_sdt;
  struct acpi_header *next;
  size_t ntables_actual;
  int cur_tab = 0;

  err = acpi_get_num_tables(&ntables_actual);
  if (err)
    return err;

  *tables = malloc(ntables_actual * sizeof(**tables));
  if (!*tables)
    return ENOMEM;

  err = acpi_get_rsdp (&rsdp);
  if (err)
    return err;

  if (rsdp.v1.revision == 0) {
    // ACPI 1.0
    sdt_base = rsdp.v1.rsdt_addr;
    is_64bit = false;
  } else if (rsdp.v1.revision == 2) {
    // ACPI >= 2.0
    sdt_base = rsdp.xsdt_addr;
    is_64bit = true;
  } else {
    return ENODEV;
  }

  /* Now we have the sdt_base address and knowledge of 32/64 bit ACPI */

  root_sdt = acpi_os_map_memory(sdt_base, ESCD_SIZE);
  if (root_sdt == MAP_FAILED)
    return errno;

  /* Get total tables */
  uint32_t ntables;
  uint8_t sz_ptr;
  sz_ptr = is_64bit ? 8 : 4;
  ntables = (root_sdt->length - sizeof(*root_sdt)) / sz_ptr;

  /* Get pointer to first ACPI table */
  void *acpi_ptr = (void*)root_sdt + sizeof(*root_sdt);

  /* Get all tables and data */
  for (int i = 0; i < ntables; i++)
    {
      if (is_64bit) {
        uint64_t acpi_ptr64;
        memcpy(&acpi_ptr64, acpi_ptr + i*sz_ptr, sizeof(acpi_ptr64));
        next = acpi_os_map_memory(acpi_ptr64, ESCD_SIZE);
      } else {
        uint32_t acpi_ptr32;
        memcpy(&acpi_ptr32, acpi_ptr + i*sz_ptr, sizeof(acpi_ptr32));
        next = acpi_os_map_memory(acpi_ptr32, ESCD_SIZE);
      }

      if (next == MAP_FAILED)
        return errno;

      if (next->signature[0] == '\0' || next->length == 0) {
        acpi_os_unmap_memory(next, ESCD_SIZE);
        continue;
      }
      uint32_t datalen = next->length - sizeof(*next);
      void *data = (void *)((uintptr_t)next + sizeof(*next));

      /* We now have a pointer to the data,
       * its length and header.
       */
      struct acpi_table *t = *tables + cur_tab;
      memcpy(&t->h, next, sizeof(*next));
      t->datalen = 0;
      t->data = malloc(datalen);
      if (!t->data) {
        acpi_os_unmap_memory(next, ESCD_SIZE);
        acpi_os_unmap_memory(root_sdt, ESCD_SIZE);
        return ENOMEM;
      }
      t->datalen = datalen;
      memcpy(t->data, data, datalen);
      cur_tab++;
      acpi_os_unmap_memory(next, ESCD_SIZE);
    }

  acpi_os_unmap_memory(root_sdt, ESCD_SIZE);

  return 0;
}
