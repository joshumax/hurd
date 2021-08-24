/*
 * Rump block driver support
 *
 * Copyright (C) 2019 Free Software Foundation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include <mach.h>
#include <hurd.h>
#include <hurd/ports.h>
#include <device/device.h>

#define MACH_INCLUDE

#include "libmachdev/machdev.h"
#include <rump/rump.h>
#include <rump/rump_syscalls.h>
#include <rump/rumperrno2host.h>

#include "ioccom-rump.h"
#define DIOCGMEDIASIZE  _IOR('d', 132, off_t)
#define DIOCGSECTORSIZE _IOR('d', 133, unsigned int)

#define BLKRRPART  _IO(0x12,95)     /* re-read partition table */

#define DISK_NAME_LEN 32
#define MAX_DISK_DEV 2

static bool disabled;

/* One of these is associated with each open instance of a device.  */
struct block_data
{
  struct port_info port;	/* device port */
  struct machdev_emul_device device;	/* generic device structure */
  dev_mode_t mode;		/* r/w etc */
  int rump_fd;			/* block device fd handle */
  char name[DISK_NAME_LEN];	/* eg /dev/wd0 */
  off_t media_size;		/* total block device size */
  uint32_t block_size;		/* size in bytes of 1 sector */
  int taken;			/* refcount */
  struct block_data *next;
};

/* Return a send right associated with network device ND.  */
static mach_port_t
rumpdisk_dev_to_port (void *nd)
{
  return (nd ? ports_get_send_right (nd) : MACH_PORT_NULL);
}

static struct block_data *block_head;
static struct machdev_device_emulation_ops rump_block_emulation_ops;

static struct block_data *
search_bd (char *name)
{
  struct block_data *bd = block_head;

  while (bd)
    {
      if (!strcmp (bd->name, name))
	return bd;
      bd = bd->next;
    }
  return NULL;
}

/* BSD name of whole disk device is /dev/rwdXd
 * but we will receive wdX as the name */
static void
translate_name (char *output, int len, char *name)
{
  snprintf (output, len - 1, "/dev/r%sd", name);
}

static boolean_t
is_disk_device (char *name)
{
  const char *dev;
  const char *allowed_devs[MAX_DISK_DEV] = {
    "wd",
    "cd"
  };
  uint8_t i;

  for (i = 0; i < MAX_DISK_DEV; i++)
    {
      dev = allowed_devs[i];
      /* /dev/XXN but we only care about /dev/XX prefix */
      if (! strncmp (dev, name, strlen(dev)))
        return TRUE;
    }
  return FALSE;
}

static int
dev_mode_to_rump_mode (const dev_mode_t mode)
{
  int ret = 0;
  if (mode & D_READ)
    {
      if (mode & D_WRITE)
	ret = RUMP_O_RDWR;
      else
	ret = RUMP_O_RDONLY;
    }
  else
    {
      if (mode & D_WRITE)
	ret = RUMP_O_WRONLY;
    }
  return ret;
}

static void
rumpdisk_device_init (void)
{
  mach_port_t device_master;

  if (! get_privileged_ports (0, &device_master))
    {
      device_t device;

#if 0
      if (! device_open (device_master, D_READ, "hd0", &device)
       || ! device_open (device_master, D_READ, "hd1", &device)
       || ! device_open (device_master, D_READ, "hd3", &device)
       || ! device_open (device_master, D_READ, "hd2", &device))
	{
	  fprintf(stderr, "Kernel is already driving an IDE device, skipping probing disks\n");
	  fflush(stderr);
	  disabled = 1;
	  return;
	}
#endif

      if (! device_open (device_master, D_READ, "sd0", &device)
       || ! device_open (device_master, D_READ, "sd1", &device)
       || ! device_open (device_master, D_READ, "sd2", &device)
       || ! device_open (device_master, D_READ, "sd3", &device))
	{
	  fprintf(stderr, "Kernel is already driving a SATA device, skipping probing disks\n");
	  fflush(stderr);
	  disabled = 1;
	  return;
	}
    }
  rump_init ();
}

static io_return_t
rumpdisk_device_close (void *d)
{
  struct block_data *bd = d;
  io_return_t err;

  bd->taken--;
  if (bd->taken)
    return D_SUCCESS;

  err = rump_errno2host (rump_sys_close (bd->rump_fd));
  if (err != D_SUCCESS)
    return err;

  ports_port_deref (bd);
  ports_destroy_right (bd);
  return D_SUCCESS;
}

static void
rumpdisk_device_dealloc (void *d)
{
  rump_sys_reboot (0, NULL);
}

static void
rumpdisk_device_sync (void)
{
  if (disabled)
    return;

  rump_sys_sync ();
}

static io_return_t
rumpdisk_device_open (mach_port_t reply_port, mach_msg_type_name_t reply_port_type,
		      dev_mode_t mode, char *name, device_t * devp,
		      mach_msg_type_name_t * devicePoly)
{
  io_return_t err;
  int ret, fd;
  struct block_data *bd;
  char dev_name[DISK_NAME_LEN];
  off_t media_size;
  uint32_t block_size;

  if (disabled)
    return D_NO_SUCH_DEVICE;

  if (! is_disk_device (name))
    return D_NO_SUCH_DEVICE;

  /* Find previous device or open if new */
  bd = search_bd (name);
  if (bd)
    {
      bd->taken++;
      *devp = ports_get_right (bd);
      *devicePoly = MACH_MSG_TYPE_MAKE_SEND;
      return D_SUCCESS;
    }

  translate_name (dev_name, DISK_NAME_LEN, name);

  fd = rump_sys_open (dev_name, dev_mode_to_rump_mode (mode));
  if (fd < 0)
    return rump_errno2host (errno);

  ret = rump_sys_ioctl (fd, DIOCGMEDIASIZE, &media_size);
  if (ret < 0)
    {
      mach_print ("DIOCGMEDIASIZE ioctl fails\n");
      return rump_errno2host (errno);
    }

  ret = rump_sys_ioctl (fd, DIOCGSECTORSIZE, &block_size);
  if (ret < 0)
    {
      mach_print ("DIOCGSECTORSIZE ioctl fails\n");
      return rump_errno2host (errno);
    }

  err = machdev_create_device_port (sizeof (*bd), &bd);
  if (err != 0)
    {
      rump_sys_close (fd);
      return err;
    }

  bd->taken = 1;
  snprintf (bd->name, DISK_NAME_LEN, "%s", name);
  bd->rump_fd = fd;
  bd->mode = mode;
  bd->device.emul_data = bd;
  bd->device.emul_ops = &rump_block_emulation_ops;
  bd->media_size = media_size;
  bd->block_size = block_size;

  bd->next = block_head;
  block_head = bd;

  *devp = ports_get_right (bd);
  *devicePoly = MACH_MSG_TYPE_MAKE_SEND;
  return D_SUCCESS;
}

static io_return_t
rumpdisk_device_write (void *d, mach_port_t reply_port,
		       mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		       recnum_t bn, io_buf_ptr_t data, unsigned int count,
		       int *bytes_written)
{
  struct block_data *bd = d;
  ssize_t written;

  if ((bd->mode & D_WRITE) == 0)
    return D_INVALID_OPERATION;

  written = rump_sys_pwrite (bd->rump_fd, (const void *)data, (size_t)count, (off_t)bn * bd->block_size);
  vm_deallocate (mach_task_self (), (vm_address_t) data, count);

  if (written < 0)
    {
      *bytes_written = 0;
      return EIO;
    }
  else
    {
      *bytes_written = (int)written;
      return D_SUCCESS;
    }
}

static io_return_t
rumpdisk_device_read (void *d, mach_port_t reply_port,
		      mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		      recnum_t bn, int count, io_buf_ptr_t * data,
		      unsigned *bytes_read)
{
  struct block_data *bd = d;
  vm_address_t buf;
  int pagesize = sysconf (_SC_PAGE_SIZE);
  int npages = (count + pagesize - 1) / pagesize;
  ssize_t err;
  kern_return_t ret;

  if ((bd->mode & D_READ) == 0)
    return D_INVALID_OPERATION;

  if (count == 0)
    return D_SUCCESS;

  *data = 0;
  ret = vm_allocate (mach_task_self (), &buf, npages * pagesize, TRUE);
  if (ret != KERN_SUCCESS)
    return ENOMEM;

  err = rump_sys_pread (bd->rump_fd, (void *)buf, (size_t)count, (off_t)bn * bd->block_size);
  if (err < 0)
    {
      *bytes_read = 0;
      vm_deallocate (mach_task_self (), buf, npages * pagesize);
      return EIO;
    }
  else
    {
      *bytes_read = err;
      *data = (void*) buf;
      return D_SUCCESS;
    }
}

static io_return_t
rumpdisk_device_set_status (void *d, dev_flavor_t flavor, dev_status_t status,
			    mach_msg_type_number_t status_count)
{
  switch (flavor)
    {
    case BLKRRPART:
      /* Partitions are not implemented here, but in the parted-based
       * translators.  */
      return D_SUCCESS;
    default:
      return D_INVALID_OPERATION;
    }
}

static io_return_t
rumpdisk_device_get_status (void *d, dev_flavor_t flavor, dev_status_t status,
			    mach_msg_type_number_t * count)
{
  struct block_data *bd = d;

  switch (flavor)
    {
    case DEV_GET_SIZE:
      status[DEV_GET_SIZE_RECORD_SIZE] = bd->block_size;
      status[DEV_GET_SIZE_DEVICE_SIZE] = bd->media_size;
      *count = 2;
      break;
    case DEV_GET_RECORDS:
      status[DEV_GET_RECORDS_RECORD_SIZE] = bd->block_size;
      status[DEV_GET_RECORDS_DEVICE_RECORDS] =
	bd->media_size / (unsigned long long) bd->block_size;
      *count = 2;
      break;
    default:
      return D_INVALID_OPERATION;
    }
  return D_SUCCESS;
}

/* FIXME:
 * Short term strategy:
 *
 * Make device_read/write multithreaded.
 * Note: this would require an rwlock between device_open/close/read/write, to
 * protect against e.g. concurrent open, unexpected close while read/write is
 * called, etc.
 *
 * Long term strategy:
 *
 * Call rump_sys_aio_read/write and return MIG_NO_REPLY from
 * device_read/write, and send the mig reply once the aio request has
 * completed. That way, only the aio request will be kept in rumpdisk
 * memory instead of a whole thread structure.
 */
static struct machdev_device_emulation_ops rump_block_emulation_ops = {
  rumpdisk_device_init,
  NULL,
  rumpdisk_device_dealloc,
  rumpdisk_dev_to_port,
  rumpdisk_device_open,
  rumpdisk_device_close,
  rumpdisk_device_write, /* FIXME: make multithreaded */
  NULL,
  rumpdisk_device_read, /* FIXME: make multithreaded */
  NULL,
  rumpdisk_device_set_status,
  rumpdisk_device_get_status,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  rumpdisk_device_sync
};

void
rump_register_block (void)
{
  machdev_register (&rump_block_emulation_ops);
}
