/* Main entry point for the ext2 file system translator

   Copyright (C) 1994, 1995, 1996 Free Software Foundation, Inc.

   Converted for ext2fs by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <stdarg.h>
#include <stdio.h>
#include <device/device.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <argz.h>
#include <version.h>
#include "ext2fs.h"

/* ---------------------------------------------------------------- */

int diskfs_link_max = EXT2_LINK_MAX;
int diskfs_maxsymlinks = 8;
int diskfs_shortcut_symlink = 1;
int diskfs_shortcut_chrdev = 1;
int diskfs_shortcut_blkdev = 1;
int diskfs_shortcut_fifo = 1;
int diskfs_shortcut_ifsock = 1;

char *diskfs_server_name = "ext2fs";
char *diskfs_server_version = HURD_VERSION;
char *diskfs_extra_version = "ext2 " EXT2FS_VERSION;

int diskfs_synchronous = 0;
int diskfs_readonly = 0;

struct node *diskfs_root_node;

#ifdef EXT2FS_DEBUG

int ext2_debug_flag = 0;

/* Ext2fs-specific options.  */
static const struct argp_option
options[] =
{
  {"debug", 'D', 0, 0, "Toggle debugging output" },
  {0}
};

/* Parse a command line option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'D':
      state->hook = (void *)1;	/* Do it at the end */
      break;

    case ARGP_KEY_INIT:
      state->hook = 0;
      break;
    case ARGP_KEY_SUCCESS:
      /* All options parse successfully, so implement ours if possible.  */
      if (state->hook)
	ext2_debug_flag = !ext2_debug_flag;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/* Add our startup arguments to the standard diskfs set.  */
static const struct argp *startup_parents[] = { &diskfs_std_device_startup_argp, 0};
static struct argp startup_argp = {options, parse_opt, 0, 0, startup_parents};

/* Similarly at runtime.  */
static const struct argp *runtime_parents[] = {&diskfs_std_runtime_argp, 0};
static struct argp runtime_argp = {options, parse_opt, 0, 0, runtime_parents};

struct argp *diskfs_runtime_argp = (struct argp *)&runtime_argp;

/* Override the standard diskfs routine so we can add our own output.  */
error_t
diskfs_get_options (char **argz, unsigned *argz_len)
{
  error_t err;

  *argz = 0;
  *argz_len = 0;

  /* Get the standard things.  */
  err = diskfs_append_std_options (argz, argz_len);

  if (!err && ext2_debug_flag)
    {
      err = argz_add (argz, argz_len, "--debug");
      if (err)
	free (argz);		/* Deallocate what diskfs returned.  */
    }

  return err;
}

#else /* !EXT2FS_DEBUG */

#define startup_argp diskfs_std_device_startup_argp

#endif /* EXT2FS_DEBUG */

void
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap = MACH_PORT_NULL;

  argp_parse (&startup_argp, argc, argv, 0, 0, 0);

  diskfs_console_stdio ();

  if (! diskfs_boot_flags)
    {
      task_get_bootstrap_port (mach_task_self (), &bootstrap);
      if (bootstrap == MACH_PORT_NULL)
	error (2, 0, "Must be started as a translator");
    }

  /* Initialize the diskfs library.  This must come before
     any other diskfs call.  */
  err = diskfs_init_diskfs ();
  if (err)
    error (4, err, "init");

  err = diskfs_device_open ();
  if (err)
    error (3, err, "%s", diskfs_device_arg);

  if ((diskfs_device_size << diskfs_log2_device_block_size)
      < SBLOCK_OFFS + SBLOCK_SIZE)
    ext2_panic ("superblock won't fit on the device!");
  if (diskfs_log2_device_block_size == 0)
    ext2_panic ("device block size (%u) not a power of two",
		diskfs_device_block_size);
  if (diskfs_log2_device_blocks_per_page < 0)
    ext2_panic ("device block size (%u) greater than page size (%d)",
		diskfs_device_block_size, vm_page_size);

  /* Map the entire disk. */
  create_disk_pager ();

  /* Start the first request thread, to handle RPCs and page requests. */
  diskfs_spawn_first_thread ();

  pokel_init (&global_pokel, disk_pager, disk_image);

  get_hypermetadata();

  inode_init ();

  /* Set diskfs_root_node to the root inode. */
  err = diskfs_cached_lookup (EXT2_ROOT_INO, &diskfs_root_node);
  if (err)
    ext2_panic ("can't get root: %s", strerror (err));
  else if ((diskfs_root_node->dn_stat.st_mode & S_IFMT) == 0)
    ext2_panic ("no root node!");
  mutex_unlock (&diskfs_root_node->lock);

  /* Now that we are all set up to handle requests, and diskfs_root_node is
     set properly, it is safe to export our fsys control port to the
     outside world.  */
  diskfs_startup_diskfs (bootstrap, 0);

  /* and so we die, leaving others to do the real work.  */
  cthread_exit (0);
}

error_t
diskfs_reload_global_state ()
{
  pokel_flush (&global_pokel);
  pager_flush (disk_pager, 1);
  get_hypermetadata ();
  return 0;
}

/* ---------------------------------------------------------------- */

static spin_lock_t free_page_bufs_lock = SPIN_LOCK_INITIALIZER;
static vm_address_t free_page_bufs = 0;

/* Returns a single page page-aligned buffer.  */
vm_address_t get_page_buf ()
{
  vm_address_t buf;

  spin_lock (&free_page_bufs_lock);

  buf = free_page_bufs;
  if (buf == 0)
    {
      error_t err;
      spin_unlock (&free_page_bufs_lock);
      err = vm_allocate (mach_task_self (), &buf, vm_page_size, 1);
      if (err)
	buf = 0;
    }
  else
    {
      free_page_bufs = *(vm_address_t *)buf;
      spin_unlock (&free_page_bufs_lock);
    }

  return buf;
}

/* Frees a block returned by get_page_buf.  */
void free_page_buf (vm_address_t buf)
{
  spin_lock (&free_page_bufs_lock);
  *(vm_address_t *)buf = free_page_bufs;
  free_page_bufs = buf;
  spin_unlock (&free_page_bufs_lock);
}
