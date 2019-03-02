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

#include "acpi.h"

int
mmap_phys_acpi_header(uintptr_t base_addr, struct acpi_header **ptr_to_header,
                      void **virt_addr, int fd)
{
  /* The memory mapping must be done aligned to page size
   * but we have a known physical address we want to inspect,
   * therefore we must compute offsets.
   */
  uintptr_t pa_acpi = base_addr & ~(sysconf(_SC_PAGE_SIZE) - 1);
  uintptr_t pa_start = base_addr - pa_acpi;

  /* Map the ACPI table at the nearest page (rounded down) */
  *virt_addr = 0;
  *virt_addr = mmap(NULL, ESCD_SIZE, PROT_READ, MAP_SHARED | MAP_FIXED,
                    fd, (off_t) pa_acpi);

  if (*virt_addr == MAP_FAILED)
    return errno;

  /* Fabricate a pointer to our magic address */
  *ptr_to_header = (struct acpi_header *)(*virt_addr + pa_start);

  return 0;
}

int
acpi_get_num_tables(size_t *num_tables)
{
  int fd_mem;
  int err;
  void *virt_addr, *virt_addr2;
  bool found = false;
  struct rsdp_descr2 rsdp = { 0 };
  uintptr_t sdt_base = (uintptr_t)0;
  bool is_64bit = false;
  unsigned char *buf;
  struct acpi_header *root_sdt;
  struct acpi_header *next;

  if ((fd_mem = open("/dev/mem", O_RDWR)) < 0)
    return EPERM;

  virt_addr = mmap(NULL, ESCD_SIZE, PROT_READ,
                   MAP_SHARED | MAP_FIXED, fd_mem, ESCD);
  if (virt_addr == MAP_FAILED)
    return errno;

  buf = (unsigned char *)virt_addr;
  found = false;

  /* RSDP magic string is 16 byte aligned */
  for (int i = 0; i < ESCD_SIZE; i += 16)
    {
      if (!memcmp(&buf[i], RSDP_MAGIC, 8)) {
        rsdp = *((struct rsdp_descr2 *)(&buf[i]));
        found = true;
        break;
      }
    }

  if (!found) {
    munmap(virt_addr, ESCD_SIZE);
    return ENODEV;
  }

  if (rsdp.v1.revision == 0) {
    // ACPI 1.0
    sdt_base = rsdp.v1.rsdt_addr;
    is_64bit = false;
  } else if (rsdp.v1.revision == 2) {
    // ACPI >= 2.0
    sdt_base = rsdp.xsdt_addr;
    is_64bit = true;
  } else {
    munmap(virt_addr, ESCD_SIZE);
    return ENODEV;
  }

  munmap(virt_addr, ESCD_SIZE);

  /* Now we have the sdt_base address and knowledge of 32/64 bit ACPI */

  err = mmap_phys_acpi_header(sdt_base, &root_sdt, &virt_addr, fd_mem);
  if (err) {
    munmap(virt_addr, ESCD_SIZE);
    return err;
  }

  /* Get total tables */
  uint32_t ntables;
  uint8_t sz_ptr;
  sz_ptr = is_64bit ? 8 : 4;
  ntables = (root_sdt->length - sizeof(*root_sdt)) / sz_ptr;

  /* Get pointer to first ACPI table */
  uintptr_t acpi_ptr = (uintptr_t)root_sdt + sizeof(*root_sdt);

  /* Get number of readable tables */
  *num_tables = 0;
  for (int i = 0; i < ntables; i++)
    {
      uintptr_t acpi_ptr32 = (uintptr_t)*((uint32_t *)(acpi_ptr + i*sz_ptr));
      uintptr_t acpi_ptr64 = (uintptr_t)*((uint64_t *)(acpi_ptr + i*sz_ptr));
      if (is_64bit) {
        err = mmap_phys_acpi_header(acpi_ptr64, &next, &virt_addr2, fd_mem);
      } else {
        err = mmap_phys_acpi_header(acpi_ptr32, &next, &virt_addr2, fd_mem);
      }

      char name[5] = { 0 };
      snprintf(name, 5, "%s", &next->signature[0]);
      if (next->signature[0] == '\0' || next->length == 0) {
        munmap(virt_addr2, ESCD_SIZE);
        continue;
      }
      *num_tables += 1;
      munmap(virt_addr2, ESCD_SIZE);
    }

  munmap(virt_addr, ESCD_SIZE);

  return 0;
}

int
acpi_get_tables(struct acpi_table **tables)
{
  int err;
  int fd_mem;
  void *virt_addr, *virt_addr2;
  uint32_t phys_addr = ESCD;
  bool found = false;
  struct rsdp_descr2 rsdp = { 0 };
  uintptr_t sdt_base = (uintptr_t)0;
  bool is_64bit = false;
  unsigned char *buf;
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

  if ((fd_mem = open("/dev/mem", O_RDWR)) < 0)
    return EPERM;

  virt_addr = mmap(NULL, ESCD_SIZE, PROT_READ, MAP_SHARED | MAP_FIXED,
                   fd_mem, (off_t) phys_addr);

  if (virt_addr == MAP_FAILED)
    return errno;

  buf = (unsigned char *)virt_addr;
  found = false;

  /* RSDP magic string is 16 byte aligned */
  for (int i = 0; i < ESCD_SIZE; i += 16)
    {
      if (!memcmp(&buf[i], RSDP_MAGIC, 8)) {
        rsdp = *((struct rsdp_descr2 *)(&buf[i]));
        found = true;
        break;
      }
    }

  if (!found) {
    munmap(virt_addr, ESCD_SIZE);
    return ENODEV;
  }

  if (rsdp.v1.revision == 0) {
    // ACPI 1.0
    sdt_base = rsdp.v1.rsdt_addr;
    is_64bit = false;
  } else if (rsdp.v1.revision == 2) {
    // ACPI >= 2.0
    sdt_base = rsdp.xsdt_addr;
    is_64bit = true;
  } else {
    munmap(virt_addr, ESCD_SIZE);
    return ENODEV;
  }

  munmap(virt_addr, ESCD_SIZE);

  /* Now we have the sdt_base address and knowledge of 32/64 bit ACPI */

  err = mmap_phys_acpi_header(sdt_base, &root_sdt, &virt_addr, fd_mem);
  if (err) {
    munmap(virt_addr, ESCD_SIZE);
    return err;
  }

  /* Get total tables */
  uint32_t ntables;
  uint8_t sz_ptr;
  sz_ptr = is_64bit ? 8 : 4;
  ntables = (root_sdt->length - sizeof(*root_sdt)) / sz_ptr;

  /* Get pointer to first ACPI table */
  uintptr_t acpi_ptr = (uintptr_t)root_sdt + sizeof(*root_sdt);

  /* Get all tables and data */
  for (int i = 0; i < ntables; i++)
    {
      uintptr_t acpi_ptr32 = (uintptr_t)*((uint32_t *)(acpi_ptr + i*sz_ptr));
      uintptr_t acpi_ptr64 = (uintptr_t)*((uint64_t *)(acpi_ptr + i*sz_ptr));
      if (is_64bit) {
        err = mmap_phys_acpi_header(acpi_ptr64, &next, &virt_addr2, fd_mem);
        if (err) {
          munmap(virt_addr, ESCD_SIZE);
          return err;
        }
      } else {
        err = mmap_phys_acpi_header(acpi_ptr32, &next, &virt_addr2, fd_mem);
        if (err) {
          munmap(virt_addr, ESCD_SIZE);
          return err;
        }
      }

      char name[5] = { 0 };
      snprintf(name, 5, "%s", &next->signature[0]);
      if (next->signature[0] == '\0' || next->length == 0) {
        munmap(virt_addr2, ESCD_SIZE);
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
        munmap(virt_addr2, ESCD_SIZE);
        munmap(virt_addr, ESCD_SIZE);
        return ENOMEM;
      }
      t->datalen = datalen;
      memcpy(t->data, data, datalen);
      cur_tab++;
      munmap(virt_addr2, ESCD_SIZE);
    }

  munmap(virt_addr, ESCD_SIZE);

  return 0;
}
