/* Main entry point for the ext2 file system translator

   Copyright (C) 1994, 95, 96, 97, 98 Free Software Foundation, Inc.

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
#include <argp.h>
#include <hurd/store.h>
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
char *diskfs_extra_version = "GNU Hurd; ext2 " EXT2FS_VERSION;

int diskfs_synchronous = 0;

struct node *diskfs_root_node;

struct store *store = 0;
struct store_parsed *store_parsed = 0;

char *diskfs_disk_name = 0;

#ifdef EXT2FS_DEBUG
int ext2_debug_flag = 0;
#endif

/* Ext2fs-specific options.  */
static const struct argp_option
options[] =
{
#ifdef EXT2FS_DEBUG
  {"debug", 'D', 0, 0, "Toggle debugging output" },
#endif
  {0}
};

/* Parse a command line option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
#ifdef EXT2FS_DEBUG
    case 'D':
      state->hook = (void *)1;	/* Do it at the end */
      break;
#endif

    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input;
#ifdef EXT2FS_DEBUG
      state->hook = 0;
#endif
      break;
    case ARGP_KEY_SUCCESS:
      /* All options parse successfully, so implement ours if possible.  */
#ifdef EXT2FS_DEBUG
      if (state->hook)
	ext2_debug_flag = !ext2_debug_flag;
#endif
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/* Override the standard diskfs routine so we can add our own output.  */
error_t
diskfs_append_args (char **argz, unsigned *argz_len)
{
  error_t err;

  /* Get the standard things.  */
  err = diskfs_append_std_options (argz, argz_len);

#ifdef EXT2FS_DEBUG
  if (!err && ext2_debug_flag)
    err = argz_add (argz, argz_len, "--debug");
#endif
  if (! err)
    err = store_parsed_append_args (store_parsed, argz, argz_len);

  return err;
}

/* Add our startup arguments to the standard diskfs set.  */
static const struct argp_child startup_children[] =
  {{&diskfs_store_startup_argp}, {0}};
static struct argp startup_argp = {options, parse_opt, 0, 0, startup_children};

/* Similarly at runtime.  */
static const struct argp_child runtime_children[] =
  {{&diskfs_std_runtime_argp}, {0}};
static struct argp runtime_argp = {options, parse_opt, 0, 0, runtime_children};

struct argp *diskfs_runtime_argp = (struct argp *)&runtime_argp;

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap = MACH_PORT_NULL;
  struct store_argp_params store_params = { 0 };

  argp_parse (&startup_argp, argc, argv, 0, 0, &store_params);
  store_parsed = store_params.result;

  err = store_parsed_name (store_parsed, &diskfs_disk_name);
  if (err)
    error (2, err, "store_parsed_name");

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

  err = store_parsed_open (store_parsed, diskfs_readonly ? STORE_READONLY : 0,
			   &store);
  if (err)
    error (3, err, "%s", diskfs_disk_name);

  if (store->size < SBLOCK_OFFS + SBLOCK_SIZE)
    ext2_panic ("superblock won't fit on the device!");
  if (store->log2_blocks_per_page < 0)
    ext2_panic ("device block size (%u) greater than page size (%d)",
		store->block_size, vm_page_size);

  if (store->flags & STORE_HARD_READONLY)
    diskfs_readonly = diskfs_hard_readonly = 1;

  /* Map the entire disk. */
  create_disk_pager ();

  /* Start the first request thread, to handle RPCs and page requests. */
  diskfs_spawn_first_thread ();

  pokel_init (&global_pokel, diskfs_disk_pager, disk_image);

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
  /* NOTREACHED */
  return 0;
}

error_t
diskfs_reload_global_state ()
{
  pokel_flush (&global_pokel);
  pager_flush (diskfs_disk_pager, 1);
  get_hypermetadata ();
  return 0;
}
