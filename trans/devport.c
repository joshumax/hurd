/* A really stupid translator allowing a user to get a device port

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdio.h>
#include <unistd.h>
#include <error.h>
#include <string.h>

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd/fsys.h>

#include <device/device.h>

static mach_port_t device_master = MACH_PORT_NULL;
static char *device_name = 0;

struct port_class *trivfs_protid_portclasses[1];
struct port_class *trivfs_cntl_portclasses[1];
int trivfs_protid_nportclasses = 1;
int trivfs_cntl_nportclasses = 1;

void
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct port_class *control_class;
  struct port_class *node_class;
  struct port_bucket *port_bucket;
  
  control_class = ports_create_class (trivfs_clean_cntl, 0);
  node_class = ports_create_class (trivfs_clean_protid, 0);
  port_bucket = ports_create_bucket ();
  trivfs_protid_portclasses[0] = node_class;
  trivfs_cntl_portclasses[0] = control_class;

  if (argc != 2)
    {
      fprintf (stderr, "Usage: %s DEVICE-NAME", program_invocation_name);
      exit(1);
    }

  device_name = argv[1];

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "must be started as a translator");

  err = get_privileged_ports (0, &device_master);
  if (err)
    error (2, err, "Can't get device master port");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, control_class, port_bucket,
			node_class, port_bucket, NULL);
  if (err)
    error (3, err, "Contacting parent");

  /* Launch. */
  ports_manage_port_operations_one_thread (port_bucket, trivfs_demuxer, 0);

  exit(0);
}

/* Trivfs hooks  */

int trivfs_fstype = FSTYPE_DEV;
int trivfs_fsid = 0;

int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;

int trivfs_allow_open = 0;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_size = 0;
  st->st_blocks = 0;
  st->st_mode &= ~S_IFMT;
  st->st_mode |= S_IFCHR;
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  exit (0);
}

error_t
trivfs_S_file_get_storage_info (struct trivfs_protid *cred,
				mach_port_t reply,
				mach_msg_type_name_t reply_type,
				mach_port_t **ports,
				mach_msg_type_name_t *ports_type,
				mach_msg_type_number_t *num_ports,
				int **ints, mach_msg_type_number_t *num_ints,
				off_t **offsets,
				mach_msg_type_number_t *num_offsets,
				char **data, mach_msg_type_number_t *data_len)
{
  error_t err = 0;

  if (!cred)
    err = EOPNOTSUPP;
  else
    {
      /* True when we've allocated memory for the corresponding vector.  */
      int al_ports = 0, al_ints = 0, al_offsets = 0, al_data = 0;
      size_t name_len = strlen (device_name) + 1;

#define ENSURE_MEM(v, vl, alp, num) 					    \
      if (!err && *vl < num)						    \
	{								    \
	  err = vm_allocate (mach_task_self (),				    \
			     (vm_address_t *)v, num * sizeof (**v), 1);	    \
	  if (! err)							    \
	    {								    \
	      *vl = num;						    \
	      alp = 1;							    \
	    }								    \
	}
		
      ENSURE_MEM (ports, num_ports, al_ports, 1);
      ENSURE_MEM (ints, num_ints, al_ints, 6);
      ENSURE_MEM (offsets, num_offsets, al_offsets, 0);
      ENSURE_MEM (data, data_len, al_data, name_len);

      if (! err)
	err = device_open (device_master, 0, device_name, *ports);
      
      if (! err)
	{
	  (*ints)[0] = STORAGE_DEVICE;	      /* type */
	  (*ints)[1] = 0;		      /* flags */
	  (*ints)[2] = 0;		      /* block_size */
	  (*ints)[3] = 0;		      /* num_runs */
	  (*ints)[4] = name_len;	      /* name_len */
	  (*ints)[5] = 0;		      /* misc_len */
	  *num_ints = 6;

	  strcpy (*data, device_name);
	  *data_len = name_len;

	  *num_ports = 1;
	  *ports_type = MACH_MSG_TYPE_MOVE_SEND;

	  *num_offsets = 0;
	}
      else
	/* Some memory allocation failed (not bloody likely).  */
	{
#define DISCARD_MEM(v, vl, alp)						    \
	  if (alp)							    \
	    vm_deallocate (mach_task_self (), (vm_address_t)*v, *vl * sizeof (**v));

	  DISCARD_MEM (ports, num_ports, al_ports);
	  DISCARD_MEM (ints, num_ints, al_ints);
	  DISCARD_MEM (offsets, num_offsets, al_offsets);
	  DISCARD_MEM (data, data_len, al_data);
	}
    }

  return err;
}
