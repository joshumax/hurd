/* Standard startup-time command line options

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdio.h>
#include <options.h>
#include "priv.h"

char *diskfs_boot_flags = 0;

mach_port_t diskfs_exec_server_task = MACH_PORT_NULL;

/* ---------------------------------------------------------------- */

#define STD_SHORT_OPTS "rwsnV"

#define OPT_HOST_PRIV_PORT	(-1)
#define OPT_DEVICE_MASTER_PORT	(-2)
#define OPT_EXEC_SERVER_TASK	(-3)
#define OPT_BOOTFLAGS		(-4)

static struct option
std_long_opts[] =
{
  {"readonly", no_argument, 0, 'r'},
  {"writable", no_argument, 0, 'w'},
  {"sync", optional_argument, 0, 's'},
  {"nosync", no_argument, 0, 'n'},
  {"version", no_argument, 0, 'V'},

  {"host-priv-port", required_argument, 0, OPT_HOST_PRIV_PORT},
  {"device-master-port", required_argument, 0, OPT_DEVICE_MASTER_PORT},
  {"exec-server-task", required_argument, 0, OPT_EXEC_SERVER_TASK},
  {"bootflags", required_argument, 0, OPT_BOOTFLAGS},

  {0, 0, 0, 0}
};

static error_t
parse_std_startup_opt (int opt, char *arg)
{
  switch (opt)
    {
    case 'r':
      diskfs_readonly = 1; break;
    case 'w':
      diskfs_readonly = 0; break;
    case 's':
      if (arg == NULL)
	diskfs_synchronous = 1;
      else
	diskfs_default_sync_interval = atoi (arg);
      break;
    case 'n':
      diskfs_synchronous = 0;
      diskfs_default_sync_interval = 0;
      break;
    case 'V':
      printf("%s %d.%d.%d\n", diskfs_server_name, diskfs_major_version,
	     diskfs_minor_version, diskfs_edit_version);
      exit(0);

      /* Boot options */
    case OPT_DEVICE_MASTER_PORT:
      _hurd_device_master = atoi (arg); break;
    case OPT_HOST_PRIV_PORT:
      _hurd_host_priv = atoi (arg); break;
    case OPT_EXEC_SERVER_TASK:
      diskfs_exec_server_task = atoi (arg); break;
    case OPT_BOOTFLAGS:
      diskfs_boot_flags = arg; break;

    default:
      return EINVAL;
    }

  return 0;
}

/* This may be used with options_parse to parse standard diskfs startup
   options, possible chained onto the end of a user options structure.  */
static struct options std_startup_opts =
  { STD_SHORT_OPTS, std_long_opts, parse_std_startup_opt, 0 };

struct options *diskfs_standard_startup_options = &std_startup_opts;
