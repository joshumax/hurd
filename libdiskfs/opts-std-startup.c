/* Standard startup-time command line parser

   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2001, 2007
     Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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
#include <hurd/paths.h>
#include "priv.h"

const char *diskfs_boot_command_line;
char **_diskfs_boot_command;

int _diskfs_boot_pause;

extern char **diskfs_argv;

mach_port_t diskfs_exec_server_task = MACH_PORT_NULL;
mach_port_t diskfs_kernel_task = MACH_PORT_NULL;

/* ---------------------------------------------------------------- */

#define OPT_HOST_PRIV_PORT	(-1)
#define OPT_DEVICE_MASTER_PORT	(-2)
#define OPT_EXEC_SERVER_TASK	(-3)
#define OPT_BOOT_CMDLINE	(-4)
#define OPT_BOOT_COMMAND	(-5)
#define OPT_BOOT_INIT_PROGRAM	(-6)
#define OPT_BOOT_PAUSE		(-7)
#define OPT_KERNEL_TASK		(-8)

static const struct argp_option
startup_options[] =
{
  {"directory",		 'C',			 "DIRECTORY", 0,
   "Use DIRECTORY as the root of the filesystem"},
  {"virtual-root",	 0, 0, OPTION_ALIAS},
  {"chroot",		 0, 0, OPTION_ALIAS},

  {0,0,0,0, "Boot options:", -2},
  {"multiboot-command-line", OPT_BOOT_CMDLINE, "ARGS", 0,
   "Required for bootstrap filesystem, the multiboot kernel command line"},
  {"bootflags", 0, 0, OPTION_ALIAS|OPTION_HIDDEN},
  {"boot-init-program",  OPT_BOOT_INIT_PROGRAM,  "FILE", 0,
   "For bootstrap filesystem, init program to run (default " _HURD_STARTUP ")"},
  {"boot-debug-pause",  OPT_BOOT_PAUSE,	         0, 0,
   "Pause for keystroke before starting bootstrap programs"},
  {"boot-command",	 OPT_BOOT_COMMAND,	 0, 0,
   "Remaining arguments form command line to run"
   " at bootstrap instead of init"},
  {"host-priv-port",     OPT_HOST_PRIV_PORT,     "PORT"},
  {"device-master-port", OPT_DEVICE_MASTER_PORT, "PORT"},
  {"exec-server-task",   OPT_EXEC_SERVER_TASK,   "PORT"},
  {"kernel-task",        OPT_KERNEL_TASK,        "PORT"},

  {0}
};

static error_t
parse_startup_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
#define TOGGLE(var, on, off) \
    case on: var = 1; break; \
    case off: var = 0; break;
      TOGGLE (diskfs_readonly, 'r', 'w');
      TOGGLE (_diskfs_nosuid, 'S', OPT_SUID_OK);
      TOGGLE (_diskfs_noexec, 'E', OPT_EXEC_OK);
      TOGGLE (_diskfs_noatime, 'A', OPT_ATIME);
      TOGGLE (_diskfs_no_inherit_dir_group, OPT_NO_INHERIT_DIR_GROUP,
	      OPT_INHERIT_DIR_GROUP);
#undef	TOGGLE

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
    case OPT_KERNEL_TASK:
      diskfs_kernel_task = atoi (arg); break;
    case OPT_BOOT_CMDLINE:
      diskfs_boot_command_line = arg; break;
    case OPT_BOOT_INIT_PROGRAM:
      diskfs_boot_init_program = arg; break;
    case OPT_BOOT_PAUSE:
      _diskfs_boot_pause = 1; break;
    case 'C':
      _diskfs_chroot_directory = arg; break;

    case OPT_BOOT_COMMAND:
      if (state->next == state->argc)
	argp_error (state, "Command line must follow --boot-command option");
      _diskfs_boot_command = state->argv + state->next;
      state->next = state->argc; /* stop parsing */
      {char **p; for (p = _diskfs_boot_command; *p; ++p) printf("BC %s\n",*p);}
      break;

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
