/* Hurd /proc filesystem, main program.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#include <mach.h>
#include <hurd.h>
#include <unistd.h>
#include <error.h>
#include <argp.h>
#include <hurd/netfs.h>
#include <ps.h>
#include "procfs.h"
#include "proclist.h"
#include "rootdir.h"
#include "dircat.h"
#include "main.h"

/* Command-line options */
int opt_clk_tck;
mode_t opt_stat_mode;
pid_t opt_fake_self;
pid_t opt_kernel_pid;
uid_t opt_anon_owner;

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  struct passwd *pw;
  char *endp;

  switch (key)
  {
    case 'h':
      opt_clk_tck = strtol (arg, &endp, 0);
      if (*endp || ! *arg || opt_clk_tck <= 0)
	argp_error (state, "--clk-tck: HZ should be a positive integer");
      break;

    case 's':
      opt_stat_mode = strtol (arg, &endp, 8);
      if (*endp || ! *arg || opt_stat_mode & ~07777)
	argp_error (state, "--stat-mode: MODE should be an octal mode");
      break;

    case 'S':
      if (arg)
        {
	  opt_fake_self = strtol (arg, &endp, 0);
	  if (*endp || ! *arg)
	    argp_error (state, "--fake-self: PID must be an integer");
	}
      else
	opt_fake_self = 1;
      break;

    case 'k':
      opt_kernel_pid = strtol (arg, &endp, 0);
      if (*endp || ! *arg || (signed) opt_kernel_pid < 0)
	argp_error (state, "--kernel-process: PID must be a positive integer");
      break;

    case 'c':
      opt_clk_tck = 100;
      opt_stat_mode = 0444;
      opt_fake_self = 1;
      break;

    case 'a':
      pw = getpwnam (arg);
      if (pw)
	{
	  opt_anon_owner = pw->pw_uid;
	  break;
	}

      opt_anon_owner = strtol (arg, &endp, 0);
      if (*endp || ! *arg || (signed) opt_anon_owner < 0)
	argp_error (state, "--anonymous-owner: USER should be "
		    "a user name or a numeric UID.");
      break;
  }

  return 0;
}

struct argp argp = {
  .options = (struct argp_option []) {
    { "clk-tck", 'h', "HZ", 0,
	"Unit used for the values expressed in system clock ticks "
	"(default: sysconf(_SC_CLK_TCK))" },
    { "stat-mode", 's', "MODE", 0,
	"The [pid]/stat file publishes information which on Hurd is only "
	"available to the process owner.  "
	"You can use this option to override its mode to be more permissive "
	"for compatibility purposes.  "
	"(default: 0400)" },
    { "fake-self", 'S', "PID", OPTION_ARG_OPTIONAL,
	"Provide a fake \"self\" symlink to the given PID, for compatibility "
	"purposes.  If PID is omitted, \"self\" will point to init.  "
	"(default: no self link)" },
    { "kernel-process", 'k', "PID", 0,
	"Process identifier for the kernel, used to retreive its command "
	"line, as well as the global up and idle times. "
	"(default: 2)" },
    { "compatible", 'c', NULL, 0,
	"Try to be compatible with the Linux procps utilities.  "
	"Currently equivalent to -h 100 -s 0444 -S 1." },
    { "anonymous-owner", 'a', "USER", 0,
	"Make USER the owner of files related to processes without one.  "
	"Be aware that USER will be granted access to the environment and "
	"other sensitive information about the processes in question.  "
	"(default: use uid 0)" },
    {}
  },
  .parser = argp_parser,
  .doc = "A virtual filesystem emulating the Linux procfs.",
  .children = (struct argp_child []) {
    { &netfs_std_startup_argp, },
    {}
  },
};

error_t
root_make_node (struct ps_context *pc, struct node **np)
{
  struct node *root_dirs[] = {
    proclist_make_node (pc),
    rootdir_make_node (pc),
  };

  *np = dircat_make_node (root_dirs, sizeof root_dirs / sizeof root_dirs[0]);
  if (! *np)
    return ENOMEM;

  /* Since this one is not created through proc_lookup(), we have to affect an
     inode number to it.  */
  (*np)->nn_stat.st_ino = * (uint32_t *) "PROC";

  return 0;
}

int main (int argc, char **argv)
{
  struct ps_context *pc;
  mach_port_t bootstrap;
  error_t err;

  opt_clk_tck = sysconf(_SC_CLK_TCK);
  opt_stat_mode = 0400;
  opt_fake_self = -1;
  opt_kernel_pid = 2;
  opt_anon_owner = 0;
  err = argp_parse (&argp, argc, argv, 0, 0, 0);
  if (err)
    error (1, err, "Could not parse command line");

  err = ps_context_create (getproc (), &pc);
  if (err)
    error (1, err, "Could not create libps context");

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  netfs_init ();
  err = root_make_node (pc, &netfs_root_node);
  if (err)
    error (1, err, "Could not create the root node");

  netfs_startup (bootstrap, 0);
  netfs_server_loop ();

  assert (0 /* netfs_server_loop returned after all */);
}

