/* A translator for providing access to Mach default_pager.defs control calls

   Copyright (C) 2002, 2007 Free Software Foundation, Inc.

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

#include <hurd/trivfs.h>
#include <unistd.h>
#include <fcntl.h>
#include <argp.h>
#include <error.h>
#include <version.h>
#include <hurd/paths.h>

#include "libtrivfs/trivfs_io_S.h"
#include "default_pager_S.h"
#include "default_pager_U.h"

static mach_port_t real_defpager, dev_master;

/* Our port class.  */
struct port_class *trivfs_protid_class;

static error_t
allowed (mach_port_t port, int mode)
{
  struct trivfs_protid *cred
    = ports_lookup_port (0, port, trivfs_protid_class);
  if (!cred)
    return MIG_BAD_ID;
  error_t result = (cred->po->openmodes & mode) ? 0 : EACCES;
  ports_port_deref (cred);
  return result;
}

kern_return_t
S_default_pager_object_create (mach_port_t default_pager,
			       memory_object_t *memory_object,
			       mach_msg_type_name_t *memory_object_type,
			       vm_size_t object_size)
{
  *memory_object_type = MACH_MSG_TYPE_COPY_SEND;
  return allowed (default_pager, O_EXEC)
    ?: default_pager_object_create (real_defpager, memory_object, object_size);
}

kern_return_t
S_default_pager_info (mach_port_t default_pager, default_pager_info_t *info)
{
  return allowed (default_pager, O_READ)
    ?: default_pager_info (real_defpager, info);
}

kern_return_t
S_default_pager_storage_info (mach_port_t default_pager,
			      vm_size_array_t *size,
			      mach_msg_type_number_t *sizeCnt,
			      vm_size_array_t *free,
			      mach_msg_type_number_t *freeCnt,
			      data_t *name,
			      mach_msg_type_number_t *nameCnt)
{
  return allowed (default_pager, O_READ)
    ?: default_pager_storage_info (real_defpager, size, sizeCnt, free, freeCnt, name, nameCnt);
}

kern_return_t
S_default_pager_objects (mach_port_t default_pager,
			 default_pager_object_array_t *objects,
			 mach_msg_type_number_t *objectsCnt,
			 mach_port_array_t *ports,
			 mach_msg_type_number_t *portsCnt)
{
  return allowed (default_pager, O_WRITE)
    ?: default_pager_objects (real_defpager,
			      objects, objectsCnt, ports, portsCnt);
}

kern_return_t
S_default_pager_object_pages (mach_port_t default_pager,
			      mach_port_t memory_object,
			      default_pager_page_array_t *pages,
			      mach_msg_type_number_t *pagesCnt)
{
  return allowed (default_pager, O_WRITE)
    ?: default_pager_object_pages (real_defpager, memory_object,
				   pages, pagesCnt);
}


kern_return_t
S_default_pager_paging_file (mach_port_t default_pager,
			     mach_port_t master_device_port,
			     default_pager_filename_t filename,
			     boolean_t add)
{
  return allowed (default_pager, O_WRITE)
    ?: default_pager_paging_file (real_defpager, dev_master, filename, add)
    ?: mach_port_deallocate (mach_task_self (), master_device_port);
}

kern_return_t
S_default_pager_paging_storage (mach_port_t default_pager,
				mach_port_t device,
				recnum_t *runs, mach_msg_type_number_t nruns,
				default_pager_filename_t name,
				boolean_t add)
{
  return allowed (default_pager, O_WRITE)
    ?: default_pager_paging_storage (real_defpager, dev_master,
				     runs, nruns, name, add)
    ?: mach_port_deallocate (mach_task_self (), device);
}

kern_return_t
S_default_pager_object_set_size (mach_port_t memory_object,
				 mach_port_seqno_t seqno,
				 vm_size_t object_size_limit)
{
  /* This is sent to an object, not the control port.  */
  return MIG_BAD_ID;
}


/* Trivfs hooks  */

int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid;

int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 1;

int trivfs_allow_open = O_READ | O_WRITE | O_EXEC;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_blksize = vm_page_size * 256; /* Make transfers LARRRRRGE */

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

kern_return_t
trivfs_S_io_read (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t replytype,
		  data_t *data,
		  mach_msg_type_number_t *datalen,
		  loff_t offs,
		  mach_msg_type_number_t amt)
{
  if (!cred)
    return EOPNOTSUPP;
  return EIO;
}

kern_return_t
trivfs_S_io_write (struct trivfs_protid *cred,
		   mach_port_t reply, mach_msg_type_name_t replytype,
		   data_t data, mach_msg_type_number_t datalen,
		   loff_t offs, mach_msg_type_number_t *amt)
{
  if (!cred)
    return EOPNOTSUPP;
  return EIO;
}

kern_return_t
trivfs_S_io_get_openmodes (struct trivfs_protid *cred,
			   mach_port_t reply, mach_msg_type_name_t replytype,
			   int *bits)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    {
      *bits = cred->po->openmodes;
      return 0;
    }
}

error_t
trivfs_S_io_set_all_openmodes (struct trivfs_protid *cred,
			       mach_port_t reply,
			       mach_msg_type_name_t replytype,
			       int mode)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return 0;
}

kern_return_t
trivfs_S_io_set_some_openmodes (struct trivfs_protid *cred,
				mach_port_t reply,
				mach_msg_type_name_t replytype,
				int bits)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return 0;
}

kern_return_t
trivfs_S_io_clear_some_openmodes (struct trivfs_protid *cred,
				  mach_port_t reply,
				  mach_msg_type_name_t replytype,
				  int bits)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return 0;
}

const char *argp_program_version = STANDARD_HURD_VERSION (proxy-defpager);

static const struct argp argp =
{doc: "\
Access to control interfaces of Mach default pager.\n\
This translator should normally be set on " _SERVERS_DEFPAGER "."};

int
proxy_defpager_demuxer (mach_msg_header_t *inp,
			mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = default_pager_server_routine (inp)) ||
      (routine = NULL, trivfs_demuxer (inp, outp)))
    {
      if (routine)
        (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;
  mach_port_t host_priv;

  argp_parse (&argp, argc, argv, 0, 0, 0);

  err = get_privileged_ports (&host_priv, &dev_master);
  if (err)
    error (2, err, "cannot get privileged ports");
  real_defpager = MACH_PORT_NULL;
  err = vm_set_default_memory_manager (host_priv, &real_defpager);
  mach_port_deallocate (mach_task_self (), host_priv);
  if (err)
    error (3, err, "vm_set_default_memory_manager");
  if (real_defpager == MACH_PORT_NULL)
    error (1, 0, "no default memory manager set!");

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  err = trivfs_add_protid_port_class (&trivfs_protid_class);
  if (err)
    error (1, 0, "error creating protid port class");

  /* Reply to our parent.  */
  err = trivfs_startup (bootstrap, 0, 0, 0, trivfs_protid_class, 0, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (4, err, "Contacting parent");

  /* Launch. */
  ports_manage_port_operations_multithread (fsys->pi.bucket,
					    proxy_defpager_demuxer,
					    2 * 60 * 1000, 0, 0);

  return 0;
}
