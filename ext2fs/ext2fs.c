/* Main entry point for the ext2 file system translator

   Copyright (C) 1994, 95, 96, 97, 98, 99 Free Software Foundation, Inc.

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
  mach_port_t bootstrap;

  /* Initialize the diskfs library, parse arguments, and open the store.
     This starts the first diskfs thread for us.  */
  store = diskfs_init_main (&startup_argp, argc, argv,
			    &store_parsed, &bootstrap);

  if (store->size < SBLOCK_OFFS + SBLOCK_SIZE)
    ext2_panic ("device too small for superblock (%ld bytes)", store->size);
  if (store->log2_blocks_per_page < 0)
    ext2_panic ("device block size (%u) greater than page size (%d)",
		store->block_size, vm_page_size);

  /* Map the entire disk. */
  create_disk_pager ();

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
