/*
 * Mach Operating System
 * Copyright (c) 1993,1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date: 	3/89
 */

/*
 * Mach device server routines (i386at version).
 *
 * Copyright (c) 1996 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 *      Author: Shantanu Goel, University of Utah CSL
 */

/*
 * This is is providing the same multiplexing layer as in the GNU Mach kernel,
 * which also handles the MIG details.
 */

#include <stdio.h>
#include <string.h>
#include <error.h>
#include <assert.h>

#include <hurd.h>
#include <mach.h>
#include <device/device.h> /* fallback to kernel device */

#include "device_S.h"
#include "notify_S.h"
#include "machdev-dev_hdr.h"
#include "machdev.h"
#include "mach_device.h"

struct port_bucket *machdev_device_bucket;
struct port_class *machdev_device_class;

#define MAX_NUM_EMULATION 32

/* List of emulations.  */
static struct machdev_device_emulation_ops *emulation_list[MAX_NUM_EMULATION];
static int num_emul = 0;

/*
 * What follows is the interface for the native Mach devices.
 */

/* Implementation of device interface */
io_return_t
ds_device_open (mach_port_t open_port, mach_port_t reply_port,
                mach_msg_type_name_t reply_port_type, dev_mode_t mode,
                const_dev_name_t name, device_t *devp,
                mach_msg_type_name_t *devicePoly)
{
  int i;
  mach_port_t dev_master;
  io_return_t err = D_NO_SUCH_DEVICE;

  /* Open must be called on the master device port.  */
  if (!machdev_is_master_device (open_port))
    return D_INVALID_OPERATION;

  /* There must be a reply port.  */
  if (! MACH_PORT_VALID (reply_port))
    return MIG_NO_REPLY;

  /* Call each emulation's open routine to find the device.  */
  for (i = 0; i < num_emul; i++)
    {
      err = (*emulation_list[i]->open) (reply_port, reply_port_type,
                                        mode, name, devp, devicePoly);
      if (err != D_NO_SUCH_DEVICE)
        break;
    }

  /* Fall back to opening kernel device master */
  if (err)
    {
      get_privileged_ports(NULL, &dev_master);
      err = device_open (dev_master, mode, name, devp);
      *devicePoly = MACH_MSG_TYPE_MOVE_SEND;
    }
  return err;
}

io_return_t
ds_device_close (struct mach_device *device)
{
  /* Refuse if device is dead or not completely open.  */
  if (device == MACH_DEVICE_NULL)
    return D_NO_SUCH_DEVICE;

  return (device->dev.emul_ops->close
	  ? (*device->dev.emul_ops->close) (device->dev.emul_data)
	  : D_SUCCESS);
}

io_return_t
ds_device_write (struct mach_device *device, mach_port_t reply_port,
		 mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		 recnum_t recnum, io_buf_ptr_t data, unsigned int count,
		 int *bytes_written)
{
  /* Refuse if device is dead or not completely open.  */
  if (device == MACH_DEVICE_NULL)
    return D_NO_SUCH_DEVICE;

  if (data == 0)
    return D_INVALID_SIZE;

  if (! device->dev.emul_ops->write)
    return D_INVALID_OPERATION;

  return (*device->dev.emul_ops->write) (device->dev.emul_data, reply_port,
					 reply_port_type, mode, recnum,
					 data, count, bytes_written);
}

io_return_t
ds_device_write_inband (struct mach_device *device, mach_port_t reply_port,
			mach_msg_type_name_t reply_port_type,
			dev_mode_t mode, recnum_t recnum,
			const io_buf_ptr_inband_t data, unsigned count,
			int *bytes_written)
{
  /* Refuse if device is dead or not completely open.  */
  if (device == MACH_DEVICE_NULL)
    return D_NO_SUCH_DEVICE;

  if (data == 0)
    return D_INVALID_SIZE;

  if (! device->dev.emul_ops->write_inband)
    return D_INVALID_OPERATION;

  return (*device->dev.emul_ops->write_inband) (device->dev.emul_data,
					        reply_port, reply_port_type,
					        mode, recnum,
					        data, count, bytes_written);
}

io_return_t
ds_device_read (struct mach_device *device, mach_port_t reply_port,
		mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		recnum_t recnum, int count, io_buf_ptr_t *data,
		unsigned *bytes_read)
{
  /* Refuse if device is dead or not completely open.  */

  if (device == MACH_DEVICE_NULL)
    return D_NO_SUCH_DEVICE;

  if (! device->dev.emul_ops->read)
    return D_INVALID_OPERATION;

  return (*device->dev.emul_ops->read) (device->dev.emul_data, reply_port,
				        reply_port_type, mode, recnum,
				        count, data, bytes_read);
}

io_return_t
ds_device_read_inband (struct mach_device *device, mach_port_t reply_port,
		       mach_msg_type_name_t reply_port_type, dev_mode_t mode,
		       recnum_t recnum, int count, io_buf_ptr_inband_t data,
		       unsigned *bytes_read)
{
  /* Refuse if device is dead or not completely open.  */

  if (device == MACH_DEVICE_NULL)
    return D_NO_SUCH_DEVICE;

  if (! device->dev.emul_ops->read_inband)
    return D_INVALID_OPERATION;

  return (*device->dev.emul_ops->read_inband) (device->dev.emul_data,
					       reply_port,
					       reply_port_type, mode, recnum,
					       count, data, bytes_read);
}

io_return_t
ds_device_set_status (struct mach_device *device, dev_flavor_t flavor,
		      dev_status_t status, mach_msg_type_number_t status_count)
{
  /* Refuse if device is dead or not completely open.  */
  if (device == MACH_DEVICE_NULL)
    return D_NO_SUCH_DEVICE;

  if (! device->dev.emul_ops->set_status)
    return D_INVALID_OPERATION;

  return (*device->dev.emul_ops->set_status) (device->dev.emul_data, flavor,
					      status, status_count);
}

io_return_t
ds_device_get_status (struct mach_device *device, dev_flavor_t flavor,
                      dev_status_t status,
		      mach_msg_type_number_t *status_count)
{
  /* Refuse if device is dead or not completely open.  */
  if (device == MACH_DEVICE_NULL)
    return D_NO_SUCH_DEVICE;

  if (! device->dev.emul_ops->get_status)
    return D_INVALID_OPERATION;

  return (*device->dev.emul_ops->get_status) (device->dev.emul_data, flavor,
					      status, status_count);
}

io_return_t
ds_device_set_filter (struct mach_device *device, mach_port_t receive_port,
                      int priority, filter_t *filter, unsigned filter_count)
{
  /* Refuse if device is dead or not completely open.  */
  if (device == MACH_DEVICE_NULL)
    return D_NO_SUCH_DEVICE;

  if (! device->dev.emul_ops->set_filter)
    return D_INVALID_OPERATION;

  return (*device->dev.emul_ops->set_filter) (device->dev.emul_data,
					      receive_port,
					      priority, filter, filter_count);
}

io_return_t
ds_device_map (struct mach_device *device, vm_prot_t prot, vm_offset_t offset,
	       vm_size_t size, mach_port_t *pager, boolean_t unmap)
{
  /* Refuse if device is dead or not completely open.  */
  if (device == MACH_DEVICE_NULL)
    return D_NO_SUCH_DEVICE;

  if (! device->dev.emul_ops->map)
    return D_INVALID_OPERATION;

  return (*device->dev.emul_ops->map) (device->dev.emul_data, prot,
				       offset, size, pager, unmap);

}

kern_return_t
ds_device_intr_register (mach_device_t dev, int id, int flags,
			 mach_port_t receive_port)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_intr_ack (mach_device_t dev, mach_port_t receive_port)
{
  return D_INVALID_OPERATION;
}

error_t
machdev_create_device_port (size_t size, void *result)
{
  return ports_create_port (machdev_device_class, machdev_device_bucket,
			    size, result);
}

void machdev_device_init()
{
  int i;

  machdev_device_bucket = ports_create_bucket ();
  machdev_device_class = ports_create_class (0, 0);

  for (i = 0; i < num_emul; i++)
    {
      if (emulation_list[i]->init)
	emulation_list[i]->init();
    }
}

void machdev_device_sync()
{
  int i;
  for (i = 0; i < num_emul; i++)
    {
      if (emulation_list[i]->sync)
        emulation_list[i]->sync();
    }
}

static int
demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = device_server_routine (inp)) ||
      (routine = notify_server_routine (inp)))
    {
      (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

void machdev_register (struct machdev_device_emulation_ops *ops)
{
  assert(num_emul < MAX_NUM_EMULATION-1);
  emulation_list[num_emul++] = ops;
}

void * machdev_server(void *arg)
{
  /* Launch.  */
  do
    {
      ports_manage_port_operations_one_thread (machdev_device_bucket, demuxer, 0);
    } while (1);

  return NULL;
}
