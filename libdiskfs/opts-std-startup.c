/* Standard startup-time command line parser

   Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.

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
#include <argp.h>
#include <hurd/store.h>
#include "priv.h"

char *diskfs_boot_flags = 0;

extern char **diskfs_argv;

mach_port_t diskfs_exec_server_task = MACH_PORT_NULL;

/* ---------------------------------------------------------------- */

#define OPT_HOST_PRIV_PORT	(-1)
#define OPT_DEVICE_MASTER_PORT	(-2)
#define OPT_EXEC_SERVER_TASK	(-3)
#define OPT_BOOTFLAGS		(-4)

static const struct argp_option
startup_options[] =
{
  {0,0,0,0, "Boot options:", -2},
  {"host-priv-port",     OPT_HOST_PRIV_PORT,     "PORT"},
  {"device-master-port", OPT_DEVICE_MASTER_PORT, "PORT"},
  {"exec-server-task",   OPT_EXEC_SERVER_TASK,   "PORT"},
  {"bootflags",          OPT_BOOTFLAGS,          "FLAGS"},

  {0}
};

static error_t
parse_startup_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    case 'r':
      diskfs_readonly = 1; break;
    case 'w':
      diskfs_readonly = 0; break;
    case 'S':
      nosuid = 1; break;
    case 'E':
      noexec = 1; break;
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


      /* Boot options */
    case OPT_DEVICE_MASTER_PORT:
      _hurd_device_master = atoi (arg); break;
    case OPT_HOST_PRIV_PORT:
      _hurd_host_priv = atoi (arg); break;
    case OPT_EXEC_SERVER_TASK:
      diskfs_exec_server_task = atoi (arg); break;
    case OPT_BOOTFLAGS:
      diskfs_boot_flags = arg; break;

    case ARGP_KEY_END:
      diskfs_argv = state->argv; break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

/* Suck in the common arguments.  */
static const struct argp startup_common_argp =
  { diskfs_common_options, parse_startup_opt };
static const struct argp_child startup_argp_children[] =
  { {&startup_common_argp}, {0} };

/* This may be used with argp_parse to parse standard diskfs startup
   options, possible chained onto the end of a user argp structure.  */
const struct argp
diskfs_startup_argp =
{
  startup_options, parse_startup_opt, 0, 0, startup_argp_children
};

static error_t
parse_store_startup_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    case ARGP_KEY_INIT:
      /* Propagate our input to our STORE_ARGP child , which it will use to
	 return what it parses.  */
      state->child_inputs[1] = state->input; break;
    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

static const struct argp_child store_argp_children[] =
  { {&diskfs_startup_argp}, {&store_argp}, {0} };

/* An argp structure for the standard diskfs command line arguments plus a
   store specification.  The address of a location in which to return the
   resulting struct store_parsed structure should be passed as the input
   argument to argp_parse; see the declaration for STORE_ARGP in
   <hurd/store.h> for more information.  */
const struct argp
diskfs_store_startup_argp =
{
  0, parse_store_startup_opt, 0, 0, store_argp_children
};
