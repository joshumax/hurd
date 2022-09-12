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

/* Implementation of ACPI operations */

#include <acpi_S.h>

#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/io.h>
#include <idvec.h>

#include <acpi/acpi_init.h>
#include "acpifs.h"

static error_t
check_permissions (struct protid *master, int flags)
{
  struct node *node;
  struct acpifs_dirent *e;

  node = master->po->np;
  e = node->nn->ln;

  /* Check whether the user has permissions to access this node */
  return entry_check_perms (master->user, e, flags);
}

error_t
S_acpi_sleep (struct protid *master,
	      int sleep_state)
{
  error_t err;

  if (!master)
    return EOPNOTSUPP;

  if (!master->user)
    return EOPNOTSUPP;

  if (!idvec_contains (master->user->uids, 0))
    return EOPNOTSUPP;

  /* Perform sleep */
  acpi_enter_sleep(sleep_state);

  /* Never reached */
  return err;
}

error_t
S_acpi_get_pci_irq (struct protid *master,
		    int bus,
		    int dev,
		    int func,
		    int *irq)
{
  error_t err;

  if (!master)
    return EOPNOTSUPP;

  err = check_permissions (master, O_READ);
  if (err)
    return err;

  *irq = acpi_get_irq_number(bus, dev, func);
  return err;
}
