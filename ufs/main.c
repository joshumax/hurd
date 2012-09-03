/*
   Copyright (C) 1994,95,96,97,98,99,2002 Free Software Foundation, Inc.

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


#include "ufs.h"
#include <stdarg.h>
#include <stdio.h>
#include <error.h>
#include <device/device.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <argz.h>
#include <argp.h>
#include <hurd/store.h>

struct node *diskfs_root_node;

struct store *store = 0;
struct store_parsed *store_parsed = 0;

char *diskfs_disk_name = 0;

/* Number of device blocks per DEV_BSIZE block.  */
unsigned log2_dev_blocks_per_dev_bsize = 0;

/* Set diskfs_root_node to the root inode. */
static void
warp_root (void)
{
  error_t err;
  err = diskfs_cached_lookup (2, &diskfs_root_node);
  assert (!err);
  pthread_mutex_unlock (&diskfs_root_node->lock);
}

/* XXX */
pthread_mutex_t printf_lock = PTHREAD_MUTEX_INITIALIZER;
int printf (const char *fmt, ...)
{
  va_list arg;
  int done;
  va_start (arg, fmt);
  pthread_mutex_lock (&printf_lock);
  done = vprintf (fmt, arg);
  pthread_mutex_unlock (&printf_lock);
  va_end (arg);
  return done;
}

int diskfs_readonly;

/* Ufs-specific options.  XXX this should be moved so it can be done at
   runtime as well as startup.  */
static const struct argp_option
options[] =
{
  {"compat", 'C', "FMT", 0,
     "FMT may be GNU, 4.4, or 4.2, and determines which filesystem extensions"
     " are written onto the disk (default is GNU)"},
  {0}
};

/* Parse a ufs-specific command line option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
      enum compat_mode mode;

    case 'C':
      if (strcasecmp (arg, "gnu") == 0)
	mode = COMPAT_GNU;
      else if (strcmp (arg, "4.4") == 0)
	mode = COMPAT_BSD44;
      else if (strcmp (arg, "4.2") == 0)
	{
	  if (sblock
	      && (sblock->fs_inodefmt == FS_44INODEFMT
		  || direct_symlink_extension))
	    {
	      argp_failure (state, 0, 0,
			    "4.2 compat mode requested on 4.4 fs");
	      return EINVAL;
	    }
	  mode = COMPAT_BSD42;
	}
      else
	{
	  argp_error (state, "%s: Unknown compatibility mode", arg);
	  return EINVAL;
	}

      state->hook = (void *)mode; /* Save it for the end.  */
      break;

    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input;
      state->hook = (void *)compat_mode; break;
    case ARGP_KEY_SUCCESS:
      compat_mode = (enum compat_mode)state->hook; break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
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

/* Override the standard diskfs routine so we can add our own output.  */
error_t
diskfs_append_args (char **argz, size_t *argz_len)
{
  error_t err;

  /* Get the standard things.  */
  err = diskfs_append_std_options (argz, argz_len);

  if (!err && compat_mode != COMPAT_GNU)
    err = argz_add (argz, argz_len,
		    ((compat_mode == COMPAT_BSD42)
		     ? "--compat=4.2"
		     : "--compat=4.4"));

  if (! err)
    err = store_parsed_append_args (store_parsed, argz, argz_len);

  return err;
}

int
main (int argc, char **argv)
{
  mach_port_t bootstrap;

  /* Initialize the diskfs library, parse arguments, and open the store.
     This starts the first diskfs thread for us.  */
  store = diskfs_init_main (&startup_argp, argc, argv,
			    &store_parsed, &bootstrap);

  if (store->block_size > DEV_BSIZE)
    error (4, 0, "%s: Bad device block size %zd (should be <= %d)",
	   diskfs_disk_name, store->block_size, DEV_BSIZE);
  if (store->size < SBSIZE + SBOFF)
    error (5, 0, "%s: Disk too small (%Ld bytes)", diskfs_disk_name,
	   store->size);

  log2_dev_blocks_per_dev_bsize = 0;
  while ((1 << log2_dev_blocks_per_dev_bsize) < DEV_BSIZE)
    log2_dev_blocks_per_dev_bsize++;
  log2_dev_blocks_per_dev_bsize -= store->log2_block_size;

  /* Map the entire disk. */
  create_disk_pager ();

  get_hypermetadata ();

  inode_init ();

  /* Find our root node.  */
  warp_root ();

  /* Now that we are all set up to handle requests, and diskfs_root_node is
     set properly, it is safe to export our fsys control port to the
     outside world.  */
  diskfs_startup_diskfs (bootstrap, 0);

  /* SET HOST NAME */

  /* And this thread is done with its work. */
  pthread_exit (NULL);

  return 0;
}

error_t
diskfs_reload_global_state ()
{
  flush_pokes ();
  pager_flush (diskfs_disk_pager, 1);
  get_hypermetadata ();
  return 0;
}
