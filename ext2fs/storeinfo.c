/* Access to file layout information

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

#include <string.h>
#include <netinet/in.h>			     /* htonl */

#include "ext2fs.h"

error_t
diskfs_S_file_get_storage_info (struct protid *cred,
				mach_port_t **ports,
				mach_msg_type_name_t *ports_type,
				mach_msg_type_number_t *num_ports,
				int **ints, mach_msg_type_number_t *num_ints,
				off_t **offsets,
				mach_msg_type_number_t *num_offsets,
				char **data, mach_msg_type_number_t *data_len)
{
  error_t err = 0;
  size_t name_len =
    (diskfs_device_name && *diskfs_device_name)
      ? strlen (diskfs_device_name) + 1 : 0;
  /* True when we've allocated memory for the corresponding vector.  */
  int al_ports = 0, al_ints = 0, al_offsets = 0, al_data = 0;

  if (! cred)
    return EOPNOTSUPP;

#define ENSURE_MEM(v, vl, alp, num)					    \
  if (!err && *vl < num)						    \
    {									    \
      err = vm_allocate (mach_task_self (),				    \
			 (vm_address_t *)v, num * sizeof (**v), 1);	    \
      if (! err)							    \
	{								    \
	  *vl = num;							    \
	  alp = 1;							    \
	}								    \
    }

  /* Two longs.  */
#define MISC_LEN (sizeof (long) * 2)

  ENSURE_MEM (ports, num_ports, al_ports, 1);
  ENSURE_MEM (ints, num_ints, al_ints, 6);
  ENSURE_MEM (data, data_len, al_data, name_len + MISC_LEN);
  /* OFFSETS is more complex, and done below.  */

  if (! err)
    {
      block_t index = 0;
      unsigned num_fs_blocks;
      off_t *run = *num_offsets ? *offsets : 0;
      struct node *node = cred->po->np;

      mutex_lock (&node->lock);

      num_fs_blocks = node->dn_stat.st_blocks >> log2_stat_blocks_per_fs_block;
      while (num_fs_blocks > 0)
	{
	  block_t block;

	  err = ext2_getblk (node, index++, 0, &block);
	  if (err == EINVAL)
	    /* Either a hole, or past the end of the file.  */
	    {
	      block = 0;
	      err = 0;
	    }
	  else if (err)
	    break;

	  block <<= log2_dev_blocks_per_fs_block;
	  if (!run
	      || ((block && run[0] >= 0) /* Neither is a hole and... */
		  ? (block != run[0] + run[1]) /* BLOCK doesn't follow RUN */
		  : (block || run[0] >= 0))) /* or one is, but not both */
	    /* Add a new run.  */
	    {
	      run += 2;
	      if (!run || run >= *offsets + *num_offsets)
		if (al_offsets)
		  /* We've already allocated space for offsets; add a new
		     page to the end of it.  */
		  {
		    err =
		      vm_allocate (mach_task_self (),
				   (vm_address_t *)&run, vm_page_size, 0);
		    if (err)
		      break;
		    *num_offsets += vm_page_size / sizeof (off_t);
		  }
		else
		  /* We've run out the space passed for inline offsets by
		     the caller, so allocate our own memory and copy
		     anything we've already stored.  */
		  {
		    off_t *old = *offsets;
		    size_t old_len = *num_offsets;
		    err =
		      vm_allocate (mach_task_self (),
				   (vm_address_t *)offsets,
				   old_len * sizeof (off_t) + vm_page_size, 1);
		    if (err)
		      break;
		    if (old_len)
		      bcopy (old, *offsets, old_len * sizeof (off_t));
		    *num_offsets = old_len + vm_page_size / sizeof (off_t);
		    run = *offsets;
		    al_offsets = 1;
		  }

	      run[0] = block ?: -1;	     /* -1 means a hole in OFFSETS */
	      run[1] = 0;		     /* will get extended just below */
	    }

	  /* Increase the size of the current run by one filesystem block.  */
	  run[1] += 1 << log2_dev_blocks_per_fs_block;

	  num_fs_blocks--;
	}

      /* Fill in PORTS.  Root gets device port, everyone else, nothing.  */
      (*ports)[0] = diskfs_isuid (0, cred) ? diskfs_device : MACH_PORT_NULL;
      *ports_type = MACH_MSG_TYPE_COPY_SEND;

      /* Fill in INTS.  */
      (*ints)[0] = STORAGE_DEVICE;	     /* type */
      (*ints)[1] = 0;			     /* flags */
      (*ints)[2] = diskfs_device_block_size; /* block size */
      (*ints)[3] = (run - *offsets) / 2;     /* num runs */
      (*ints)[4] = name_len;
      (*ints)[5] = MISC_LEN;

      /* Fill in DATA.  */
      if (name_len)
	strcpy (*data, diskfs_device_name);
      /* The following must be kept in sync with MISC_LEN.  */
      ((long *)(*data + name_len))[0] = htonl (node->cache_id);
      ((long *)(*data + name_len))[1] =
	htonl (dino (node->cache_id)->i_translator);

      mutex_unlock (&node->lock);
    }

  if (err)
    {
#define DISCARD_MEM(v, vl, alp)						    \
      if (alp)								    \
	vm_deallocate (mach_task_self (), (vm_address_t)*v, *vl * sizeof **v);
      DISCARD_MEM (ports, num_ports, al_ports);
      DISCARD_MEM (ints, num_ints, al_ints);
      DISCARD_MEM (offsets, num_offsets, al_offsets);
      DISCARD_MEM (data, data_len, al_data);
    }

  return err;
}
