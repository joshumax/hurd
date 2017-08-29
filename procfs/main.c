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
#include <argz.h>
#include <hurd/netfs.h>
#include <ps.h>
#include <pids.h>
#include "procfs.h"
#include "proclist.h"
#include "rootdir.h"
#include "dircat.h"
#include "main.h"

/* Command-line options */
int opt_clk_tck;
mode_t opt_stat_mode;
pid_t opt_kernel_pid;
uid_t opt_anon_owner;

/* Default values */
#define OPT_CLK_TCK    sysconf(_SC_CLK_TCK)
#define OPT_STAT_MODE  0400
#define OPT_KERNEL_PID HURD_PID_KERNEL
#define OPT_ANON_OWNER 0

#define NODEV_KEY  -1 /* <= 0, so no short option. */
#define NOEXEC_KEY -2 /* Likewise. */
#define NOSUID_KEY -3 /* Likewise. */

static void set_compatibility_options (void)
{
  opt_clk_tck = 100;
  opt_stat_mode = 0444;
}

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  struct passwd *pw;
  char *endp;
  long int v;

  switch (key)
  {
    case 'h':
      v = strtol (arg, &endp, 0);
      if (*endp || ! *arg || v <= 0)
	argp_error (state, "--clk-tck: HZ should be a positive integer");
      else
	opt_clk_tck = v;
      break;

    case 's':
      v = strtol (arg, &endp, 8);
      if (*endp || ! *arg || (mode_t) v & ~07777)
	argp_error (state, "--stat-mode: MODE should be an octal mode");
      else
	opt_stat_mode = v;
      break;

    case 'k':
      v = strtol (arg, &endp, 0);
      if (*endp || ! *arg || (signed) opt_kernel_pid < 0)
	argp_error (state, "--kernel-process: PID must be a positive integer");
      else
	opt_kernel_pid = v;
      break;

    case 'c':
      set_compatibility_options();
      break;

    case 'a':
      pw = getpwnam (arg);
      if (pw)
	{
	  opt_anon_owner = pw->pw_uid;
	  break;
	}

      v = strtol (arg, &endp, 0);
      if (*endp || ! *arg || v < 0)
	argp_error (state, "--anonymous-owner: USER should be "
		    "a user name or a numeric UID.");
      else
	opt_anon_owner = v;
      break;

    case NODEV_KEY:
      /* Ignored for compatibility with Linux' procfs. */
      break;

    case NOEXEC_KEY:
      /* Ignored for compatibility with Linux' procfs. */
      break;

    case NOSUID_KEY:
      /* Ignored for compatibility with Linux' procfs. */
      break;

    case ARGP_KEY_ARG:
      if (!strcmp (arg, "none") || !strcmp (arg,"proc"))
        /* Ignored for compatibility with Linux' procfs. */
        break;
      return ARGP_ERR_UNKNOWN;

    default:
      return ARGP_ERR_UNKNOWN;
  }

  return 0;
}

struct argp_option common_options[] = {
#define STR(X)	XSTR (X)
#define XSTR(X)	#X
  { "clk-tck", 'h', "HZ", 0,
      "Unit used for the values expressed in system clock ticks "
      "(default: " STR (OPT_CLK_TCK) ")" },
  { "stat-mode", 's', "MODE", 0,
      "The [pid]/stat file publishes information which on Hurd is only "
      "available to the process owner.  "
      "You can use this option to override its mode to be more permissive "
      "for compatibility purposes.  "
      "(default: " STR (OPT_STAT_MODE) ")" },
  { "kernel-process", 'k', "PID", 0,
      "Process identifier for the kernel, used to retrieve its command "
      "line, as well as the global up and idle times. "
      "(default: " STR (OPT_KERNEL_PID) ")" },
  { "compatible", 'c', NULL, 0,
      "Try to be compatible with the Linux procps utilities.  "
      "Currently equivalent to -h 100 -s 0444 -S 1." },
  { "anonymous-owner", 'a', "USER", 0,
      "Make USER the owner of files related to processes without one.  "
      "Be aware that USER will be granted access to the environment and "
      "other sensitive information about the processes in question.  "
      "(default: use uid " STR (OPT_ANON_OWNER) ")" },
  { "nodev", NODEV_KEY, NULL, 0,
      "Ignored for compatibility with Linux' procfs." },
  { "noexec", NOEXEC_KEY, NULL, 0,
      "Ignored for compatibility with Linux' procfs." },
  { "nosuid", NOSUID_KEY, NULL, 0,
      "Ignored for compatibility with Linux' procfs." },
  {}
#undef XSTR
#undef STR
};

struct argp argp = {
  .options = common_options,
  .parser = argp_parser,
  .doc = "A virtual filesystem emulating the Linux procfs.",
  .children = (struct argp_child []) {
    { &netfs_std_startup_argp, },
    {}
  },
};

static error_t
runtime_argp_parser (int key, char *arg, struct argp_state *state)
{
  switch (key)
  {
    case 'u':
      /* do nothing */
      break;

    default:
      return ARGP_ERR_UNKNOWN;
  }

  return 0;
}

struct argp runtime_argp = {
  .options = (struct argp_option []) {
    { "update", 'u', NULL, 0, "remount; for procfs this does nothing" },
    {},
  },
  .parser = runtime_argp_parser,
};

struct argp netfs_runtime_argp_ = {
  .options = common_options,
  .parser = argp_parser,
  .doc = "A virtual filesystem emulating the Linux procfs.",
  .children = (struct argp_child []) {
    { &runtime_argp, },
    { &netfs_std_runtime_argp, },
    {}
  },
};

/* Used by netfs_set_options to handle runtime option parsing.  */
struct argp *netfs_runtime_argp = &netfs_runtime_argp_;

/* Return an argz string describing the current options.  Fill *ARGZ
   with a pointer to newly malloced storage holding the list and *LEN
   to the length of that storage.  */
error_t
netfs_append_args (char **argz, size_t *argz_len)
{
  char buf[80];
  error_t err = 0;

#define FOPT(opt, default, fmt, args...)             \
  do { \
    if (! err && opt != default) \
      { \
	snprintf (buf, sizeof buf, fmt, ## args); \
	err = argz_add (argz, argz_len, buf); \
      } \
  } while (0)

  FOPT (opt_clk_tck, OPT_CLK_TCK,
        "--clk-tck=%d", opt_clk_tck);

  FOPT (opt_stat_mode, OPT_STAT_MODE,
        "--stat-mode=%o", opt_stat_mode);

  FOPT (opt_anon_owner, OPT_ANON_OWNER,
        "--anonymous-owner=%d", opt_anon_owner);

  FOPT (opt_kernel_pid, OPT_KERNEL_PID,
        "--kernel-process=%d", opt_kernel_pid);

#undef FOPT

  if (! err)
    err = netfs_append_std_options (argz, argz_len);

  return err;
}

/* The user may define this function.  The function must set source to
   the source of the translator. The function may return an EOPNOTSUPP
   to indicate that the concept of a source device is not
   applicable. The default function always returns EOPNOTSUPP.  */
error_t netfs_get_source (char *source, size_t source_len)
{
  snprintf (source, source_len, "proc");
  return 0;
}

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

  opt_clk_tck = OPT_CLK_TCK;
  opt_stat_mode = OPT_STAT_MODE;
  opt_kernel_pid = OPT_KERNEL_PID;
  opt_anon_owner = OPT_ANON_OWNER;
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

  assert_backtrace (0 /* netfs_server_loop returned after all */);
}

