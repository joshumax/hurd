/* Implements the hurd io interface to devio.

   Copyright (C) 1995 Free Software Foundation, Inc.

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

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <stdio.h>
#include <fcntl.h>

#include "open.h"
#include "dev.h"
#include "iostate.h"

/* ---------------------------------------------------------------- */

/* Return objects mapping the data underlying this memory object.  If
   the object can be read then memobjrd will be provided; if the
   object can be written then memobjwr will be provided.  For objects
   where read data and write data are the same, these objects will be
   equal, otherwise they will be disjoint.  Servers are permitted to
   implement io_map but not io_map_cntl.  Some objects do not provide
   mapping; they will set none of the ports and return an error.  Such
   objects can still be accessed by io_read and io_write.  */
kern_return_t
trivfs_S_io_map(struct trivfs_protid *cred,
		memory_object_t *rdobj,
		mach_msg_type_name_t *rdtype,
		memory_object_t *wrobj,
		mach_msg_type_name_t *wrtype)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    {
      mach_port_t memobj;
      struct open *open = (struct open *)cred->po->hook;
      error_t err = dev_get_memory_object(open->dev, &memobj);

      if (!err)
	{
	  if (cred->po->openmodes & O_READ)
	    {
	      *rdobj = memobj;
	      *rdtype = MACH_MSG_TYPE_MOVE_SEND;
	    }
	  else
	    *rdobj = MACH_PORT_NULL;

	  if (cred->po->openmodes & O_WRITE)
	    {
	      *wrobj = memobj;
	      *wrtype = MACH_MSG_TYPE_MOVE_SEND;
	    }
	  else
	    *wrobj = MACH_PORT_NULL;
	}

      return err;
    }
}

/* ---------------------------------------------------------------- */

/* Read data from an IO object.  If offset if -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in AMT.  */
kern_return_t
trivfs_S_io_read(struct trivfs_protid *cred,
		 mach_port_t reply, mach_msg_type_name_t replytype,
		 vm_address_t *data,
		 mach_msg_type_number_t *datalen,
		 off_t offs,
		 mach_msg_type_number_t amt)
{
  if (!cred)
    return EOPNOTSUPP;
  else if (!(cred->po->openmodes & O_READ))
    return EBADF;
  else
    return open_read((struct open *)cred->po->hook, data, datalen, amt, offs);
}

/* ---------------------------------------------------------------- */

/* Tell how much data can be read from the object without blocking for
   a "long time" (this should be the same meaning of "long time" used
   by the nonblocking flag.  */
kern_return_t
trivfs_S_io_readable (struct trivfs_protid *cred,
		      mach_port_t reply, mach_msg_type_name_t replytype,
		      mach_msg_type_number_t *amount)
{
  if (!cred)
    return EOPNOTSUPP;
  else if (!(cred->po->openmodes & O_READ))
    return EINVAL;
  else
    {
      struct open *open = (struct open *)cred->po->hook;
      struct dev *dev = open->dev;
      vm_offset_t location = open_get_io_state(open)->location;
      *amount = dev->size - location;
      return 0;
    }
}

/* ---------------------------------------------------------------- */

/* Change current read/write offset */
kern_return_t
trivfs_S_io_seek (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t replytype,
		  off_t offs, int whence, off_t *new_offs)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return open_seek ((struct open *)cred->po->hook, offs, whence, new_offs);
}

/* ---------------------------------------------------------------- */

/* SELECT_TYPE is the bitwise OR of SELECT_READ, SELECT_WRITE, and SELECT_URG.
   Block until one of the indicated types of i/o can be done "quickly", and
   return the types that are then available.  ID_TAG is returned as passed; it
   is just for the convenience of the user in matching up reply messages with
   specific requests sent.  */
kern_return_t
trivfs_S_io_select (struct trivfs_protid *cred,
		    mach_port_t reply, mach_msg_type_name_t replytype,
		    int *type, int *tag)
{
  if (!cred)
    return EOPNOTSUPP;
  else if (((*type & SELECT_READ) && !(cred->po->openmodes & O_READ))
	   || ((*type & SELECT_WRITE) && !(cred->po->openmodes & O_WRITE)))
    return EBADF;
  else
    *type &= ~SELECT_URG;
  return 0;
}

/* ---------------------------------------------------------------- */

/* Write data to an IO object.  If offset is -1, write at the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount successfully written is returned in amount.  A
   given user should not have more than one outstanding io_write on an
   object at a time; servers implement congestion control by delaying
   responses to io_write.  Servers may drop data (returning ENOBUFS)
   if they recevie more than one write when not prepared for it.  */
kern_return_t
trivfs_S_io_write (struct trivfs_protid *cred,
		   mach_port_t reply, mach_msg_type_name_t replytype,
		   vm_address_t data, mach_msg_type_number_t datalen,
		   off_t offs, mach_msg_type_number_t *amt)
{
  if (!cred)
    return EOPNOTSUPP;
  else if (!(cred->po->openmodes & O_WRITE))
    return EBADF;
  else
    return open_write((struct open *)cred->po->hook, data, datalen, amt, offs);
}

/* ---------------------------------------------------------------- */

/* Truncate file.  */
kern_return_t
trivfs_S_file_set_size (struct trivfs_protid *cred, off_t size)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return 0;
}

/* ---------------------------------------------------------------- */
/* These four routines modify the O_APPEND, O_ASYNC, O_FSYNC, and
   O_NONBLOCK bits for the IO object. In addition, io_get_openmodes
   will tell you which of O_READ, O_WRITE, and O_EXEC the object can
   be used for.  The O_ASYNC bit affects icky async I/O; good async 
   I/O is done through io_async which is orthogonal to these calls. */

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
trivfs_S_io_set_all_openmodes(struct trivfs_protid *cred,
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

/* ---------------------------------------------------------------- */
/* Get/set the owner of the IO object.  For terminals, this affects
   controlling terminal behavior (see term_become_ctty).  For all
   objects this affects old-style async IO.  Negative values represent
   pgrps.  This has nothing to do with the owner of a file (as
   returned by io_stat, and as used for various permission checks by
   filesystems).  An owner of 0 indicates that there is no owner.  */

kern_return_t
trivfs_S_io_get_owner (struct trivfs_protid *cred,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       pid_t *owner)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    {
      struct open *open = (struct open *)cred->po->hook;
      *owner = open->dev->owner;
      return 0;
    }
}

kern_return_t
trivfs_S_io_mod_owner (struct trivfs_protid *cred,
		       mach_port_t reply, mach_msg_type_name_t replytype,
		       pid_t owner)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    {
      struct open *open = (struct open *)cred->po->hook;
      open->dev->owner = owner;
      return 0;
    }
}


/* ---------------------------------------------------------------- */
/* File syncing operations; these all do the same thing, sync the underlying
   device.  */

kern_return_t
trivfs_S_file_sync (struct trivfs_protid *cred, int wait)
{
  if (cred)
    return dev_sync (((struct open *)cred->po->hook)->dev, wait);
  else
    return EOPNOTSUPP;
}

kern_return_t
trivfs_S_file_syncfs (struct trivfs_protid *cred, int wait, int dochildren)
{
  if (!cred)
    return dev_sync (((struct open *)cred->po->hook)->dev, wait);
  else
    return EOPNOTSUPP;
}

/* ---------------------------------------------------------------- */

error_t
trivfs_S_file_get_storage_info (struct trivfs_protid *cred, int *class,
				off_t **runs, unsigned *runs_len,
				size_t *block_size,
				char *dev_name, mach_port_t *dev_port,
				mach_msg_type_name_t *dev_port_type,
				char **misc, unsigned *misc_len,
				int *flags)
{
  error_t err;
  if (!cred)
    err = EOPNOTSUPP;
  else
    {
      struct dev *dev = ((struct open *)cred->po->hook)->dev;

      err = vm_allocate (mach_task_self (),
			 (vm_address_t *)runs, 2 * sizeof (off_t), 1);
      if (!err)
	{
	  *class = STORAGE_DEVICE;
	  *flags = 0;

	  (*runs)[0] = 0;
	  (*runs)[1] = dev->size / dev->dev_block_size;
	  *runs_len = 2;

	  *block_size = dev->dev_block_size;

	  strcpy (dev_name, dev->name);

	  if (cred->isroot)
	    *dev_port = dev->port;
	  else
	    *dev_port = MACH_PORT_NULL;
	  *dev_port_type = MACH_MSG_TYPE_COPY_SEND;

	  *misc_len = 0;
	}
    }
  return err;
}
