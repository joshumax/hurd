/* Partition storeio backend
   Copyright (C) 2026 Free Software Foundation, Inc.

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <parted/parted.h>
#include <hurd/store.h>

#include "dev.h"

/* Initialize a PedDevice using SOURCE.  The SOURCE will NOT be destroyed;
   the caller created it, it is the caller's responsilbility to free it
   after it calls ped_device_destroy.  SOURCE is not registered in Parted's
   list of devices.  */
PedDevice* ped_device_new_from_store (struct store *source);

static int
set_last_partition_num (struct store *store)
{
  debug ("last_partition_num (store: %p):\n", store);
  int last_partition_num = 0;
  ped_exception_fetch_all ();
  PedDevice *device = ped_device_new_from_store (store);
  if (!device || !ped_device_open (device))
    {
      debug ("!device || !ped_device_open (device)\n");
      goto err_ped_exception_leave_all;
    }

  PedDisk *disk = ped_disk_new (device);
  if (!disk)
    {
      debug ("!disk\n");
      goto err_ped_device_close;
    }

  if (strcmp (disk->type->name, "loop") == 0)
    {
      debug ("strcmp (disk->type->name, \"loop\") == 0\n");
      goto err_ped_disk_destroy;
    }

  last_partition_num = ped_disk_get_last_partition_num (disk);
  if (last_partition_num < 0)
    {
      debug ("last_partition_num < 0\n");
      last_partition_num = 0;
    }

 err_ped_disk_destroy:
  ped_disk_destroy (disk);

 err_ped_device_close:
  if (!ped_device_close (device))
    debug ("!ped_device_close (device)\n");

 err_ped_exception_leave_all:
  ped_exception_leave_all ();

  debug ("set_last_partition_num return: %d\n", last_partition_num);
  return last_partition_num;
}

static inline char *
create_node_name (const size_t num)
{
  char *buffer;
  error_t err = asprintf (&buffer, "%zu", num);
  if (err == -1)
    return NULL;

  return buffer;
}

error_t
create_partitions (void)
{
  debug ("create_partitions:\n");
  struct node *dir = netfs_root_node;
  size_t last_partition_num =
    (size_t) set_last_partition_num (dir->nn->dev->store);
  if (last_partition_num == 0)
    {
      debug ("last_partition_num == 0\n");
      debug ("create_partitions return: ENOTDIR\n");
      return ENOTDIR;
    }

  error_t err;
  dir->nn->entries_num = last_partition_num;
  dir->nn->entries = malloc (last_partition_num * sizeof (struct node *));
  if (!dir->nn->entries)
    {
      err = errno;
      debug ("!dir->nn->entries\n");
      debug ("create_partitions return: %d\n", err);
      return err;
    }

  const int flags = ((storeio_stat.readonly ? STORE_READONLY : 0)
                     | (storeio_stat.no_fileio ? STORE_NO_FILEIO : 0));

  struct store *source, *store;
  struct node **part;
  char *node_name;
  for (size_t i = 1; i <= last_partition_num; ++i)
    {
      err = store_parsed_open (storeio_stat.store_name, flags, &source);
      if (err)
        {
          debug ("store_parsed_open return err: %d\n", err);
          break;
        }

      err = store_part_create (source, i, flags, &store);
      if (err)
        {
          debug ("store_part_create return err: %d\n", err);
          break;
        }

      part = &dir->nn->entries[i - 1];
      node_name = create_node_name (i);
      if (!node_name)
        {
          err = errno;
          debug ("!node_name\n");
          break;
        }

      err = create_node (part, node_name, dir);
      if (err)
        {
          debug ("create_node return err: %d\n", err);
          break;
        }

      err = check_dev (*part, store, flags);
      if (err)
        {
          debug ("dev_init_from_store return err: %d\n", err);
          break;
        }
    }

  debug ("create_partitions return: %d\n", err);
  return err;
}
