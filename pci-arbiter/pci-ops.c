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

/* Implementation of PCI operations */

#include <pci_S.h>

#include <fcntl.h>
#include <hurd/netfs.h>
#include <sys/mman.h>

#include <pciaccess.h>
#include "pcifs.h"
#include "func_files.h"

static error_t
check_permissions (struct protid *master, int flags)
{
  error_t err = 0;
  struct node *node;
  struct pcifs_dirent *e;

  node = master->po->np;
  e = node->nn->ln;

  /* Check wheter the user has permissions to access this node */
  err = entry_check_perms (master->user, e, flags);
  if (err)
    return err;

  /* Check wheter the request has been sent to the proper node */
  if (e->domain != 0		/* Only domain 0 can be accessed by I/O ports */
      || e->bus < 0 || e->dev < 0 || e->func < 0)
    err = EINVAL;

  return err;
}

static size_t
calculate_ndevs (struct iouser *user)
{
  size_t ndevs = 0;
  int i;
  struct pcifs_dirent *e;

  for (i = 0, e = fs->entries; i < fs->num_entries; i++, e++)
    {
      if (e->func < 0		/* Skip entries without a full address  */
	  || !S_ISDIR (e->stat.st_mode))	/* and entries that are not folders     */
	continue;

      if (!entry_check_perms (user, e, O_READ))
	/* If no error, user may access this device */
	ndevs++;
    }

  return ndevs;
}

/*
 * Read min(amount,*datalen) bytes and store them on `*data'.
 *
 * `*datalen' is updated.
 */
error_t
S_pci_conf_read (struct protid * master, int reg, char **data,
		 size_t * datalen, mach_msg_type_number_t amount)
{
  error_t err;
  pthread_mutex_t *lock;
  struct pcifs_dirent *e;

  if (!master)
    return EOPNOTSUPP;

  e = master->po->np->nn->ln;
  if (strncmp (e->name, FILE_CONFIG_NAME, NAME_SIZE))
    /* This operation may only be addressed to the config file */
    return EINVAL;

  lock = &fs->pci_conf_lock;

  err = check_permissions (master, O_READ);
  if (err)
    return err;

  /*
   * We don't allocate new memory since we expect no more than 4 bytes-long
   * buffers. Instead, we just take the lower value as length.
   */
  if (amount > *datalen)
    amount = *datalen;

  /*
   * The server is not single-threaded anymore. Incoming rpcs are handled by
   * libnetfs which is multi-threaded. A lock is needed for arbitration.
   */
  pthread_mutex_lock (lock);
  err = pci_device_cfg_read (e->device, *data, reg, amount, NULL);
  pthread_mutex_unlock (lock);

  if (!err)
    {
      *datalen = amount;
      /* Update atime */
      UPDATE_TIMES (e, TOUCH_ATIME);
    }

  return err;
}

/* Write `datalen' bytes from `data'. `amount' is updated. */
error_t
S_pci_conf_write (struct protid * master, int reg, char *data, size_t datalen,
		  mach_msg_type_number_t * amount)
{
  error_t err;
  pthread_mutex_t *lock;
  struct pcifs_dirent *e;

  if (!master)
    return EOPNOTSUPP;

  e = master->po->np->nn->ln;
  if (strncmp (e->name, FILE_CONFIG_NAME, NAME_SIZE))
    /* This operation may only be addressed to the config file */
    return EINVAL;

  lock = &fs->pci_conf_lock;

  err = check_permissions (master, O_WRITE);
  if (err)
    return err;

  pthread_mutex_lock (lock);
  err = pci_device_cfg_write (e->device, data, reg, datalen, NULL);
  pthread_mutex_unlock (lock);

  if (!err)
    {
      *amount = datalen;
      /* Update mtime and ctime */
      UPDATE_TIMES (e, TOUCH_MTIME | TOUCH_CTIME);
    }

  return err;
}

/* Write in `amount' the number of devices allowed for the user. */
error_t
S_pci_get_ndevs (struct protid * master, mach_msg_type_number_t * amount)
{
  /* This RPC may only be addressed to the root node */
  if (master->po->np != fs->root)
    return EINVAL;

  *amount = calculate_ndevs (master->user);

  return 0;
}

/*
 * Return in `data' the information about the available memory
 * regions in the given device.
 */
error_t
S_pci_get_dev_regions (struct protid * master, char **data, size_t * datalen)
{
  error_t err;
  struct pcifs_dirent *e;
  struct pci_bar regions[6], *r;
  size_t size;
  int i;

  if (!master)
    return EOPNOTSUPP;

  e = master->po->np->nn->ln;
  if (strncmp (e->name, FILE_CONFIG_NAME, NAME_SIZE))
    /* This operation may only be addressed to the config file */
    return EINVAL;

  err = check_permissions (master, O_READ);
  if (err)
    return err;

  /* Allocate memory if needed */
  size = sizeof (regions);
  if (size > *datalen)
    {
      *data = mmap (0, size, PROT_READ | PROT_WRITE, MAP_ANON, 0, 0);
      if (*data == MAP_FAILED)
	return ENOMEM;
    }

  /* Copy the regions info */
  for (i = 0, r = (struct pci_bar *) *data; i < 6; i++, r++)
    {
      r->base_addr = e->device->regions[i].base_addr;
      r->size = e->device->regions[i].size;
      r->is_IO = e->device->regions[i].is_IO;
      r->is_prefetchable = e->device->regions[i].is_prefetchable;
      r->is_64 = e->device->regions[i].is_64;
    }

  /* Update atime */
  UPDATE_TIMES (e, TOUCH_ATIME);

  *datalen = size;

  return 0;
}

/*
 * Return in `data' the information about the expansion rom of the given device
 */
error_t
S_pci_get_dev_rom (struct protid * master, char **data, size_t * datalen)
{
  error_t err;
  struct pcifs_dirent *e;
  struct pci_xrom_bar rom;
  size_t size;

  if (!master)
    return EOPNOTSUPP;

  e = master->po->np->nn->ln;
  if (strncmp (e->name, FILE_CONFIG_NAME, NAME_SIZE))
    /* This operation may only be addressed to the config file */
    return EINVAL;

  err = check_permissions (master, O_READ);
  if (err)
    return err;

  /* Allocate memory if needed */
  size = sizeof (rom);
  if (size > *datalen)
    {
      *data = mmap (0, size, PROT_READ | PROT_WRITE, MAP_ANON, 0, 0);
      if (*data == MAP_FAILED)
	return ENOMEM;
    }

  /* Copy the regions info */
  rom.base_addr = 0; // pci_device_private only
  rom.size = e->device->rom_size;
  memcpy (*data, &rom, size);

  /* Update atime */
  UPDATE_TIMES (e, TOUCH_ATIME);

  *datalen = size;

  return 0;
}
