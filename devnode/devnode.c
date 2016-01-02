/* 
   Copyright (C) 2008 Free Software Foundation, Inc.
   Written by Zheng Da.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/*
 * This program is a translator that sits on the top of the network
 * interface and helps socket servers open the interface.
 */

#include <argp.h>
#include <argz.h>
#include <errno.h>
#include <error.h>
#include <stddef.h>
#include <fcntl.h>

#include <hurd.h>
#include <mach.h>
#include <device/device.h>
#include <hurd/trivfs.h>
#include <hurd/ports.h>
#include <version.h>

#include "device_S.h"
#include "notify_S.h"
#include "util.h"

/* The name of the network interface that the translator sits on. */
static char *device_name;
/* The device name used by the socket servers. */
static char *user_device_name;
static char *master_file;
/* The master device port for opening the interface. */
static mach_port_t master_device;

const char *argp_program_version = STANDARD_HURD_VERSION (devnode);

static const char args_doc[] = "device";
static const char doc[] = "Hurd devnode translator.";
static const struct argp_option options[] =
{
    {"name", 'n', "DEVICENAME", 0,
      "Define the device name used by clients in device_open()", 2},
    {"master-device", 'M', "FILE", 0, 
      "Get a pseudo master device port", 3},
    {0}
};

/* Port bucket we service requests on.  */
struct port_bucket *port_bucket;

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = O_READ | O_WRITE;

/* Our port classes.  */
struct port_class *trivfs_protid_class;
struct port_class *trivfs_cntl_class;

static int
devnode_demuxer (mach_msg_header_t *inp,
		    mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = device_server_routine (inp)) ||
      (routine = notify_server_routine (inp)) ||
      (routine = NULL, trivfs_demuxer (inp, outp)))
    {
      if (routine)
        (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

/* Implementation of notify interface */
kern_return_t
do_mach_notify_port_deleted (struct port_info *pi,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_msg_accepted (struct port_info *pi,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_port_destroyed (struct port_info *pi,
			       mach_port_t port)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_no_senders (struct port_info *pi,
			   mach_port_mscount_t mscount)
{
  return ports_do_mach_notify_no_senders (pi, mscount);
}

kern_return_t
do_mach_notify_send_once (struct port_info *pi)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_dead_name (struct port_info *pi,
			  mach_port_t name)
{
  return EOPNOTSUPP;
}

/* Implementation of device interface */
kern_return_t
ds_device_open (mach_port_t master_port, mach_port_t reply_port,
		mach_msg_type_name_t reply_portPoly,
		dev_mode_t mode, dev_name_t name, mach_port_t *device, 
		mach_msg_type_name_t *devicetype)
{
  error_t err;

  devnode_debug ("ds_device_open is called\n");

  if ((user_device_name && strcmp (user_device_name, name))
      || device_name == NULL) 
      return D_NO_SUCH_DEVICE;

  if (master_file != NULL)
    {
      mach_port_t md;
      if (MACH_PORT_VALID (master_device))
        mach_port_deallocate (mach_task_self (), master_device);
      md = file_name_lookup (master_file, 0, 0);
      if (MACH_PORT_VALID (md))
        master_device = md;
      else
        {
          error (0, 0, "%s: %s.\nFalling back to kernel driver.",
                 master_file, strerror (errno));
          err = get_privileged_ports (0, &master_device);
          if (err)
            return err;
        }
    }

  err = device_open (master_device, mode, device_name, device); 
  *devicetype = MACH_MSG_TYPE_MOVE_SEND;
  return err;
}

kern_return_t
ds_device_close (device_t device)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_write (device_t device, mach_port_t reply_port,
		 mach_msg_type_name_t reply_type, dev_mode_t mode,
		 recnum_t recnum, io_buf_ptr_t data, size_t datalen,
		 int *bytes_written)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_write_inband (device_t device, mach_port_t reply_port,
			mach_msg_type_name_t reply_type, dev_mode_t mode,
			recnum_t recnum, io_buf_ptr_inband_t data,
			size_t datalen, int *bytes_written)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_read (device_t device, mach_port_t reply_port,
		mach_msg_type_name_t reply_type, dev_mode_t mode,
		recnum_t recnum, int bytes_wanted,
		io_buf_ptr_t *data, size_t *datalen)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_read_inband (device_t device, mach_port_t reply_port,
		       mach_msg_type_name_t reply_type, dev_mode_t mode,
		       recnum_t recnum, int bytes_wanted,
		       io_buf_ptr_inband_t data, size_t *datalen)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_map (device_t device, vm_prot_t prot, vm_offset_t offset,
	       vm_size_t size, memory_object_t *pager, int unmap)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_set_status (device_t device, dev_flavor_t flavor,
		      dev_status_t status, size_t statuslen)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_get_status (device_t device, dev_flavor_t flavor,
		      dev_status_t status, size_t *statuslen)
{
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_set_filter (device_t device, mach_port_t receive_port,
		      int priority, filter_array_t filter, size_t filterlen)
{
  return D_INVALID_OPERATION;
}

error_t
trivfs_append_args (struct trivfs_control *fsys, char **argz, size_t *argz_len)
{
  error_t err = 0;

#define ADD_OPT(fmt, args...)						\
  do { char buf[100];							\
       if (! err) {							\
         snprintf (buf, sizeof buf, fmt , ##args);			\
         err = argz_add (argz, argz_len, buf); } } while (0)

  if (user_device_name)
    ADD_OPT ("--name=%s", user_device_name);
  if (master_file)
    ADD_OPT ("--master-device=%s", master_file);

  ADD_OPT ("%s", device_name);

#undef ADD_OPT
  return err;
}

void
trivfs_modify_stat (struct trivfs_protid *cred, io_statbuf_t *stat)
{
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  int count;

  /* Stop new requests.  */
  ports_inhibit_class_rpcs (trivfs_cntl_class);
  ports_inhibit_class_rpcs (trivfs_protid_class);

  count = ports_count_class (trivfs_protid_class);
  devnode_debug ("the number of ports alive: %d\n", count);

  if (count && !(flags & FSYS_GOAWAY_FORCE)) 
    {
      /* We won't go away, so start things going again...  */
      ports_enable_class (trivfs_protid_class);
      ports_resume_class_rpcs (trivfs_cntl_class);
      ports_resume_class_rpcs (trivfs_protid_class);
      return EBUSY;
    } 

  mach_port_deallocate (mach_task_self (), master_device);
  devnode_debug ("the translator is gone away\n");
  exit (0);
}

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    case 'M':
      master_file = arg;
      master_device = file_name_lookup (arg, 0, 0);
      if (master_device == MACH_PORT_NULL)
	error (0, 0, "%s: %s.\nFalling back to kernel driver.",
               arg, strerror (errno));
      break;
    case 'n':
      user_device_name = arg;
      break;
    case ARGP_KEY_ARG:
      device_name = arg;
      break;
    case ARGP_KEY_ERROR:
    case ARGP_KEY_SUCCESS:
    case ARGP_KEY_INIT:
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

int
main (int argc, char *argv[])
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;
  const struct argp argp = { options, parse_opt, args_doc, doc };

  port_bucket = ports_create_bucket ();
  trivfs_cntl_class = ports_create_class (trivfs_clean_cntl, 0);
  trivfs_protid_class = ports_create_class (trivfs_clean_protid, 0);

  argp_parse (&argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "must be started as a translator");

  if (master_device == MACH_PORT_NULL) 
    {
      err = get_privileged_ports (0, &master_device);
      if (err)
	error (1, err, "get_privileged_ports");
    }

  /* Reply to our parent.  */
  err = trivfs_startup (bootstrap, 0,
			trivfs_cntl_class, port_bucket,
			trivfs_protid_class, port_bucket, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (1, err, "Contacting parent");

  /* Launch.  */
  do 
    {
      ports_manage_port_operations_one_thread (port_bucket, 
					       devnode_demuxer, 0);
    } while (trivfs_goaway (fsys, 0)); 
  return 0;
}
