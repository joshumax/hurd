/* Main entry point for the ext2 file system translator

   Copyright (C) 1994,95,96,97,98,99,2002 Free Software Foundation, Inc.

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
int diskfs_name_max = EXT2_NAME_LEN;
int diskfs_maxsymlinks = 8;
int diskfs_shortcut_symlink = 1;
int diskfs_shortcut_chrdev = 1;
int diskfs_shortcut_blkdev = 1;
int diskfs_shortcut_fifo = 1;
int diskfs_shortcut_ifsock = 1;

char *diskfs_server_name = "ext2fs";
char *diskfs_server_version = HURD_VERSION;
char *diskfs_extra_version = "GNU Hurd; ext2 " EXT2FS_VERSION;

int diskfs_synchronous;

struct node *diskfs_root_node;

struct store *store;
struct store_parsed *store_parsed;

char *diskfs_disk_name;

pthread_spinlock_t global_lock = PTHREAD_SPINLOCK_INITIALIZER;
pthread_spinlock_t modified_global_blocks_lock = PTHREAD_SPINLOCK_INITIALIZER;

#ifdef EXT2FS_DEBUG
int ext2_debug_flag;
#endif

/* Use extended attribute-based translator records.  */
int use_xattr_translator_records;
#define X_XATTR_TRANSLATOR_RECORDS	-1

/* Ext2fs-specific options.  */
static const struct argp_option
options[] =
{
  {"debug", 'D', 0, 0, "Toggle debugging output"
#ifndef EXT2FS_DEBUG
   " (not compiled in)"
#endif
  },
  {"x-xattr-translator-records", X_XATTR_TRANSLATOR_RECORDS, 0, 0,
   "Store translator records in extended attributes (experimental)"},
#ifdef ALTERNATE_SBLOCK
  /* XXX This is not implemented.  */
  {"sblock", 'S', "BLOCKNO", 0,
   "Use alternate superblock location (1kb blocks)"},
#endif
  {0}
};

/* Parse a command line option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  /* We save our parsed values in this structure, hung off STATE->hook.
     Only after parsing all options successfully will we use these values.  */
  struct
  {
    int debug_flag;
    int use_xattr_translator_records;
#ifdef ALTERNATE_SBLOCK
    unsigned int sb_block;
#endif
  } *values = state->hook;

  switch (key)
    {
    case 'D':
      values->debug_flag = 1;
      break;
    case X_XATTR_TRANSLATOR_RECORDS:
      values->use_xattr_translator_records = 1;
      break;
#ifdef ALTERNATE_SBLOCK
    case 'S':
      values->sb_block = strtoul (arg, &arg, 0);
      if (!arg || *arg != '\0')
	{
	  argp_error (state, "invalid number for --sblock");
	  return EINVAL;
	}
      break;
#endif

    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input;
      values = malloc (sizeof *values);
      if (values == 0)
	return ENOMEM;
      state->hook = values;
      memset (values, 0, sizeof *values);
#ifdef ALTERNATE_SBLOCK
      values->sb_block = SBLOCK_BLOCK;
#endif
      break;

    case ARGP_KEY_SUCCESS:
      /* All options parse successfully, so implement ours if possible.  */
      if (values->debug_flag)
	{
#ifdef EXT2FS_DEBUG
	  ext2_debug_flag = !ext2_debug_flag;
#else
	  argp_failure (state, 2, 0, "debugging support not compiled in");
	  return EINVAL;
#endif
	}

      use_xattr_translator_records = values->use_xattr_translator_records;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/* Override the standard diskfs routine so we can add our own output.  */
error_t
diskfs_append_args (char **argz, size_t *argz_len)
{
  error_t err;

  /* Get the standard things.  */
  err = diskfs_append_std_options (argz, argz_len);

  if (!err && use_xattr_translator_records)
    err = argz_add (argz, argz_len, "--x-xattr-translator-records");

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
    ext2_panic ("device too small for superblock (%Ld bytes)", store->size);
  if (store->log2_blocks_per_page < 0)
    ext2_panic ("device block size (%zu) greater than page size (%zd)",
		store->block_size, vm_page_size);

  /* Map the entire disk. */
  create_disk_pager ();

  pokel_init (&global_pokel, diskfs_disk_pager, disk_cache);

  map_hypermetadata ();

  /* Set diskfs_root_node to the root inode. */
  err = diskfs_cached_lookup (EXT2_ROOT_INO, &diskfs_root_node);
  if (err)
    ext2_panic ("can't get root: %s", strerror (err));
  else if ((diskfs_root_node->dn_stat.st_mode & S_IFMT) == 0)
    ext2_panic ("no root node!");
  pthread_mutex_unlock (&diskfs_root_node->lock);

  /* Now that we are all set up to handle requests, and diskfs_root_node is
     set properly, it is safe to export our fsys control port to the
     outside world.  */
  diskfs_startup_diskfs (bootstrap, 0);

  /* and so we die, leaving others to do the real work.  */
  pthread_exit (NULL);
  /* NOTREACHED */
  return 0;
}

error_t
diskfs_reload_global_state ()
{
  error_t err;

  pokel_flush (&global_pokel);
  pager_flush (diskfs_disk_pager, 1);

  /* libdiskfs is not responsible for inhibiting paging.  */
  err = inhibit_ext2_pager ();
  if (err)
    return err;

  get_hypermetadata ();
  map_hypermetadata ();

  resume_ext2_pager ();

  return 0;
}
