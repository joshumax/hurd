/* diskfs_init_main -- initialize diskfs world, parse arguments, and open store
   Copyright (C) 1999, 2001 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "diskfs.h"
#include <argp.h>
#include <assert-backtrace.h>
#include <error.h>
#include <hurd/store.h>

struct store *
diskfs_init_main (struct argp *startup_argp,
		  int argc, char **argv,
		  struct store_parsed **store_parsed,
		  mach_port_t *bootstrap)
{
  error_t err;
  struct store_argp_params store_params = { 0 };
  struct store *store;

  /* We must use ARGP_IN_ORDER for the parsing of --boot-command to work.  */
  err = argp_parse (startup_argp ?: &diskfs_store_startup_argp,
		    argc, argv, ARGP_IN_ORDER, NULL,
		    &store_params);
  assert_perror_backtrace (err);
  *store_parsed = store_params.result;

  err = store_parsed_name (*store_parsed, &diskfs_disk_name);
  if (err)
    error (2, err, "store_parsed_name");

  /* This must come after the args have been parsed, as this is where the
     host priv ports are set for booting.  */
  diskfs_console_stdio ();

  if (diskfs_boot_filesystem ())
    /* We are the bootstrap filesystem.  */
    *bootstrap = MACH_PORT_NULL;
  else
    {
      task_get_bootstrap_port (mach_task_self (), bootstrap);
      if (*bootstrap == MACH_PORT_NULL)
	error (2, 0, "Must be started as a translator");
    }

  /* Initialize the diskfs library.  Must come before any other diskfs call. */
  err = diskfs_init_diskfs ();
  if (err)
    error (4, err, "diskfs_init_diskfs");

  err = store_parsed_open (*store_parsed, diskfs_readonly ? STORE_READONLY : 0,
			   &store);
  if (err)
    error (3, err, "%s", diskfs_disk_name);

  if (store->flags & STORE_HARD_READONLY)
    diskfs_readonly = diskfs_hard_readonly = 1;

  /* Start the first request thread, to handle RPCs and page requests. */
  diskfs_spawn_first_thread (diskfs_demuxer);

  return store;
}
