/*
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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

char *ufs_version = "0.0 pre-alpha";

struct node *diskfs_root_node;

/* Set diskfs_root_node to the root inode. */
static void
warp_root (void)
{
  error_t err;
  err = diskfs_cached_lookup (2, &diskfs_root_node);
  assert (!err);
  mutex_unlock (&diskfs_root_node->lock);
}

/* XXX */
struct mutex printf_lock = MUTEX_INITIALIZER;
int printf (const char *fmt, ...)
{
  va_list arg;
  int done;
  va_start (arg, fmt);
  mutex_lock (&printf_lock);
  done = vprintf (fmt, arg);
  mutex_unlock (&printf_lock);
  va_end (arg);
  return done;
}

int diskfs_readonly;

int
main (int argc, char **argv)
{
  error_t err;
  off_t disk_size;
  mach_port_t bootstrap;

  argp_parse (diskfs_device_startup_argp, argc, argv, 0, 0, 0);

  /* This must come after the args have been parsed, as this is where the
     host priv ports are set for booting.  */
  diskfs_console_stdio ();

  if (diskfs_boot_flags)
    {
      /* We are the bootstrap filesystem.  */
      bootstrap = MACH_PORT_NULL;
      diskfs_use_mach_device = 1;
      compat_mode = COMPAT_GNU;
    }
  else
    {
      task_get_bootstrap_port (mach_task_self (), &bootstrap);
      if (bootstrap == MACH_PORT_NULL)
	error (2, 0, "Must be started as a translator");
    }

  /* Initialize the diskfs library.  Must come before any other diskfs call. */
  err = diskfs_init_diskfs ();
  if (err)
    error (4, err, "init");

  err = diskfs_device_open ();
  if (err)
    error (3, err, "%s", diskfs_device_arg);

  if (diskfs_device_block_size != DEV_BSIZE)
    error (4, err, "%s: Bad device record size %d (should be %d)",
	   diskfs_device_arg, diskfs_device_block_size, DEV_BSIZE);
  if (diskfs_log2_device_block_size == 0)
    error (4, err, "%s: Device block size (%d) not a power of 2",
	   diskfs_device_arg, diskfs_device_block_size);

  disk_size = diskfs_device_size << diskfs_log2_device_block_size;
  assert (disk_size >= SBSIZE + SBOFF);

  /* Map the entire disk. */
  create_disk_pager ();

  /* Start the first request thread, to handle RPCs and page requests. */
  diskfs_spawn_first_thread ();

  get_hypermetadata ();

  inode_init ();

  /* Find our root node.  */
  warp_root ();

  /* Now that we are all set up to handle requests, and diskfs_root_node is
     set properly, it is safe to export our fsys control port to the
     outside world.  */
  diskfs_startup_diskfs (bootstrap, 0);

  /* And this thread is done with its work. */
  cthread_exit (0);

  return 0;
}

error_t
diskfs_reload_global_state ()
{
  flush_pokes ();
  pager_flush (disk_pager, 1);
  get_hypermetadata ();
  return 0;
}
