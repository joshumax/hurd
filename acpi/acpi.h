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

/* ACPI tables basic structure */

#ifndef ACPI_H
#define ACPI_H

#include <stdlib.h>
#include <inttypes.h>

/* PnP Extended System Configuration Data (ESCD) memory region */
#define ESCD		0xe0000U
#define  RSDP_MAGIC	(const unsigned char *)"RSD PTR "
#define ESCD_SIZE	0x20000U

struct rsdp_descr
{
  uint8_t magic[8];
  uint8_t checksum;
  uint8_t oem_id[6];
  uint8_t revision;
  uint32_t rsdt_addr;
} __attribute__ ((packed));

struct rsdp_descr2
{
  struct rsdp_descr v1;
  uint32_t length;
  uint64_t xsdt_addr;
  uint8_t checksum;
  uint8_t reserved[3];
} __attribute__ ((packed));

struct acpi_header
{
  uint8_t signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  uint8_t oem_id[6];
  uint8_t oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
} __attribute__ ((packed));

struct acpi_table
{
  struct acpi_header h;
  void *data;
  size_t datalen;
} __attribute__ ((packed));

int acpi_get_num_tables(size_t *num_tables);
int acpi_get_tables(struct acpi_table **tables);

#endif /* ACPI_H */
