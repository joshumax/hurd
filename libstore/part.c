/* Partition store backend
   Copyright (C) 2001, 2002 Free Software Foundation, Inc.
   Written by Neal H Walfield <neal@cs.uml.edu>

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include "store.h"
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <cthreads.h>

#include <parted/parted.h>
/*#include <parted/device_gnu.h>*/
#include <string.h>
#include <error.h>

#define NEED_PARTED_VERSION "1.5.4"
#ifndef PED_SECTOR_SIZE
#define PED_SECTOR_SIZE PED_SECTOR_SIZE_DEFAULT
#endif

/* Return a new store in STORE which contains a remap store of partition
   PART from the contents of SOURCE; SOURCE is consumed.  */
error_t
store_part_create (struct store *source, int index, int flags,
		   struct store **store)
{
  static struct mutex parted_lock = MUTEX_INITIALIZER;
  static int version_check;
  error_t err = 0;
  PedDevice *dev;
  PedDisk *disk;
  PedPartition *part;
  struct store_run run;

  if ((source->block_size < PED_SECTOR_SIZE
       && PED_SECTOR_SIZE % source->block_size != 0)
      || (source->block_size > PED_SECTOR_SIZE
	  && source->block_size % PED_SECTOR_SIZE != 0))
    return EINVAL;

  mutex_lock (&parted_lock);

  /* Since Parted provides no source-level information about
     version compatibility, we have to check at run time.  */
  if (version_check == 0)
    {
      const char *version = ped_get_version ();
      version_check = -1;
      if (version == 0)
	error (0, 0, "cannot get version of Parted library!");
      else if (strverscmp (version, NEED_PARTED_VERSION) < 0)
	error (0, 0, "Parted library version %s older than needed %s",
	       version, NEED_PARTED_VERSION);
      else
	version_check = 1;
    }
  if (version_check <= 0)
    {
      error (0, 0, "the `part' store type is not available");
      mutex_unlock (&parted_lock);
      return ENOTSUP;
    }

  ped_exception_fetch_all ();

  dev = ped_device_new_from_store (source);
  if (! dev)
    {
      ped_exception_catch ();
      err = EIO;
      goto out;
    }

  assert (ped_device_open (dev) != 0);

  disk = ped_disk_new (dev);
  if (! disk)
    {
      ped_exception_catch ();
      err = EIO;
      goto out_with_dev;
    }

  for (part = ped_disk_next_partition (disk, NULL); part;
       part = ped_disk_next_partition (disk, part))
    {
      if (part->type != PED_PARTITION_LOGICAL
	  && part->type != 0 /* PED_PARTITION_PRIMARY */)
	continue;

      assert (part->num);
      if (part->num == index)
        break;
    }

  if (! part)
    {
      err = EIO;
      goto out_with_disk;
    }

  if (source->block_size == PED_SECTOR_SIZE)
    {
      run.start = part->geom.start;
      run.length = part->geom.length;
    }
  else if (source->block_size < PED_SECTOR_SIZE)
    {
      run.start = part->geom.start * (PED_SECTOR_SIZE / source->block_size);
      run.length = part->geom.length * (PED_SECTOR_SIZE / source->block_size);
    }
  else
    /* source->block_size > PED_SECTOR_SIZE */
    {
      run.start = part->geom.start * PED_SECTOR_SIZE;
      if (run.start % source->block_size != 0)
	err = EIO;
      else
	{
	  run.start /= source->block_size;
          run.length = part->geom.length * PED_SECTOR_SIZE;
	  if (run.length % source->block_size != 0)
	    err = EIO;
	  else
	    run.length /= source->block_size;
	}
    }

out_with_disk:
  assert (ped_device_close (dev) != 0);
  ped_disk_destroy (disk);
out_with_dev:
  ped_device_destroy (dev);
out:
  ped_exception_leave_all ();
  mutex_unlock (&parted_lock);

  if (! err)
    err = store_remap (source, &run, 1, store);

  return err;
}

/* Open the part NAME.  NAME consists of a partition number, a ':', a another
   store class name, a ':' and a name for to by passed to the store class.
   E.g. "2:device:hd0" would open the second partition on a DEVICE store
   named "hd0".  FLAGS indicate how to open the store.  CLASSES is used to
   select classes specified by the type NAME; if it is 0, STORE_STD_CLASSES
   is used.  The new store is returned in *STORE.  */
error_t
store_part_open (const char *name, int flags,
		 const struct store_class *const *classes,
		 struct store **store)
{
  int part;
  char *endp;
  struct store *source;
  error_t err;

  part = strtol (name, &endp, 0);
  if (endp == name || *endp != ':')
    return EINVAL;

  name = endp + 1;
  if (*name == '\0')
    return EINVAL;

  err = store_typed_open (name, flags, classes, &source);
  if (! err)
    {
      err = store_part_create (source, part, flags, store);
      if (err)
	store_free (source);
    }

  return err;
}

const struct store_class
store_part_class = { -1, "part", open: store_part_open };
STORE_STD_CLASS (part);
