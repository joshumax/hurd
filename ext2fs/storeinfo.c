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

#include <netinet/in.h>

#include "ext2fs.h"

error_t
diskfs_S_file_get_storage_info (struct protid *cred, int *class,
				off_t **runs, unsigned *runs_len,
				size_t *block_size,
				char *dev_name, mach_port_t *dev_port,
				mach_msg_type_name_t *dev_port_type,
				char **misc, unsigned *misc_len,
				int *flags)
{
  error_t err = 0;
  block_t index = 0;
  unsigned num_fs_blocks;
  unsigned runs_alloced = 0;
  off_t *run = 0;
  struct node *node = cred->po->np;

  *misc_len = sizeof (long) * 4;
  err = vm_allocate (mach_task_self (), (vm_address_t *)misc, *misc_len, 1);
  if (err)
    return err;

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
      if (err)
	goto fail;

      block <<= log2_dev_blocks_per_fs_block;
      if (!run
	  || ((block && run[0] >= 0)        /* Neither is a hole and... */
	      ? (block != run[0] + run[1])  /* ... BLOCK doesn't follow RUN */
	      : (block || run[0] >= 0)))    /* or ... one is, but not both */
	/* Add a new run.  */
	{
	  if (run)
	    /* There are already some runs.  */
	    {
	      run += 2;
	      if (run >= *runs + runs_alloced)
		/* Add a new page to the end of the existing RUNS array.  */
		{
		  err = vm_allocate (mach_task_self (),
				     (vm_address_t *)&run, vm_page_size, 0);
		  if (err)
		    goto fail;
		  runs_alloced += vm_page_size / sizeof (off_t);
		}
	    }
	  else
	    /* Allocate the RUNS array for the first time.  */
	    {
	      err = vm_allocate (mach_task_self (),
				 (vm_address_t *)runs, vm_page_size, 1);
	      if (err)
		goto fail;
	      runs_alloced = vm_page_size / sizeof (off_t);
	      run = *runs;
	    }

	  run[0] = block ?: -1;	/* -1 means a hole in RUNS */
	  run[1] = 0;		/* will get extended just below */
	}

      /* Increase the size of the current run by one filesystem block.  */
      run[1] += 1 << log2_dev_blocks_per_fs_block;

      num_fs_blocks--;
    }

  if (run)
    {
      if (run[0] >= 0)
	/* Include the current run, as long as it's not a hole.  */
	run += 2;
      else if ((off_t *)trunc_page (run) == run)
	/* We allocated just *one* too many pages -- the last run is a hole. */
	vm_deallocate (mach_task_self (), (vm_address_t)run, vm_page_size);
      *runs_len = run - *runs;
    }
  else
    *runs_len = 0;

  ((long *)*misc)[0] = htonl (node->cache_id);
  ((long *)*misc)[1] = htonl (dino (node->cache_id)->i_translator);

  *class = STORAGE_DEVICE;
  *flags = 0;

  *block_size = diskfs_device_block_size;

  strcpy (dev_name, diskfs_device_name);

  if (diskfs_isuid (0, cred))
    *dev_port = diskfs_device;
  else
    *dev_port = MACH_PORT_NULL;
  *dev_port_type = MACH_MSG_TYPE_COPY_SEND;

 fail:
  mutex_unlock (&node->lock);

  if (err)
    {
      if (*runs_len > 0)
	vm_deallocate (mach_task_self (), (vm_address_t)*runs,
		       runs_alloced * sizeof (off_t));
      vm_deallocate (mach_task_self (), (vm_address_t)*misc, *misc_len);
    }

  return err;
}
