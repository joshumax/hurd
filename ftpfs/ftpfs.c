/* Ftp filesystem

   Copyright (C) 1997 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <string.h>
#include <unistd.h>
#include <argp.h>
#include <error.h>
#include <argz.h>
#include <netdb.h>

#include <hurd/netfs.h>

#include "ftpfs.h"

static char *args_doc = "REMOTE_FS [SERVER]";
static char *doc = "Hurd ftp filesystem translator"
"\vIf SERVER is not specified, an attempt is made to extract"
" it from REMOTE_FS, using `SERVER:FS' notation."
"  SERVER can be a hostname, in which case anonymous ftp is used,"
" or may include a user and password like `USER:PASSWORD@HOST' (the"
" `:PASSWORD' part is optional).";

/* The filesystem.  */
struct ftpfs *ftpfs;

/* Parameters describing the server we're connecting to.  */
struct ftp_conn_params *ftpfs_ftp_params = 0;

/* customization hooks.  */
struct ftp_conn_hooks ftpfs_ftp_hooks = { interrupt_check: hurd_check_cancel };

/* The (user-specified) name of the SERVER:FILESYSTEM we're connected too.  */
char *ftpfs_remote_fs;

/* The FILESYSTEM component of FTPFS_REMOTE_FS.  */
char *ftpfs_remote_root;

/* Random parameters for the filesystem.  */
struct ftpfs_params ftpfs_params;

volatile struct mapped_time_value *ftpfs_maptime;

int netfs_maxsymlinks = 0;

extern error_t lookup_server (const char *server,
			      struct ftp_conn_params **params, int *h_err);

/* Prints ftp connection log to stderr.  */
static void
cntl_debug (struct ftp_conn *conn, int type, const char *txt)
{
  char *type_str;
  static struct mutex debug_lock = MUTEX_INITIALIZER;

  switch (type)
    {
    case FTP_CONN_CNTL_DEBUG_CMD:   type_str = ">"; break;
    case FTP_CONN_CNTL_DEBUG_REPLY: type_str = "="; break;
    default: type_str = "?"; break;
    }

  mutex_lock (&debug_lock);
  fprintf (stderr, "%u.%s%s\n", (unsigned)conn->hook, type_str, txt);
  mutex_unlock (&debug_lock);
}

/* Various default parameters.  */
#define DEFAULT_NAME_TIMEOUT	300
#define DEFAULT_STAT_TIMEOUT	120

#define DEFAULT_BULK_STAT_PERIOD     10
#define DEFAULT_BULK_STAT_THRESHOLD  5

#define DEFAULT_NODE_CACHE_MAX	50

/* Return a string corresponding to the printed rep of DEFAULT_what */
#define ___D(what) #what
#define __D(what) ___D(what)
#define _D(what) __D(DEFAULT_ ## what)

/* Common (runtime & startup) options.  */

#define OPT_NO_DEBUG	        1

#define OPT_NAME_TIMEOUT        5
#define OPT_STAT_TIMEOUT        7
#define OPT_NODE_CACHE_MAX      8
#define OPT_BULK_STAT_PERIOD    9
#define OPT_BULK_STAT_THRESHOLD 10

/* Options usable both at startup and at runtime.  */
static const struct argp_option common_options[] =
{
  {"debug",    'D', 0,     0, "Turn on debugging output for ftp connections"},
  {"no-debug", OPT_NO_DEBUG, 0, OPTION_HIDDEN },

  {0,0,0,0, "Parameters:"},
  {"name-timeout",  OPT_NAME_TIMEOUT,     "SECS", 0,
   "Time directory names are cached (default " _D(NAME_TIMEOUT) ")"},
  {"stat-timeout",    OPT_STAT_TIMEOUT,   "SECS", 0,
   "Time stat information is cached (default " _D(STAT_TIMEOUT) ")"},
  {"node-cache-size", OPT_NODE_CACHE_MAX, "ENTRIES", 0,
   "Number of recently used filesystem nodes that are cached (default "
   _D(NODE_CACHE_MAX) ")"},

  {"bulk-stat-period",    OPT_BULK_STAT_PERIOD,    "SECS", 0,
   "Period for detecting bulk stats (default " _D(BULK_STAT_PERIOD) ")"},
  {"bulk-stat-threshold", OPT_BULK_STAT_THRESHOLD, "SECS", 0,
   "Number of stats within the bulk-stat-period that trigger a bulk stat"
   " (default " _D(BULK_STAT_THRESHOLD) ")"}, 

  {0, 0}
};

static error_t
parse_common_opt (int key, char *arg, struct argp_state *state)
{
  struct ftpfs_params *params = state->input;

  switch (key)
    {
    case 'D':
      ftpfs_ftp_hooks.cntl_debug = cntl_debug; break;
    case OPT_NO_DEBUG:
      ftpfs_ftp_hooks.cntl_debug = 0; break;

    case OPT_NODE_CACHE_MAX:
      params->node_cache_max = atoi (arg); break;
    case OPT_NAME_TIMEOUT:
      params->name_timeout = atoi (arg); break;
    case OPT_STAT_TIMEOUT:
      params->stat_timeout = atoi (arg); break;
    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

static struct argp common_argp = { common_options, parse_common_opt };

/* Startup options.  */

static const struct argp_option startup_options[] =
{
  { 0 }
};

/* Parse a single command line option/argument.  */
static error_t
parse_startup_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case ARGP_KEY_ARG:
      if (state->arg_num > 1)
	argp_usage (state);
      else if (state->arg_num == 0)
	ftpfs_remote_fs = arg;
      else
	/* If the fs & server are two separate args, glom them together into the
	   ":" notation.  */
	{
	  char *rfs = malloc (strlen (ftpfs_remote_fs) + 1 + strlen (arg) + 1);
	  if (! rfs)
	    argp_failure (state, 99, ENOMEM, "%s", arg);
	  stpcpy (stpcpy (stpcpy (rfs, arg), ":"), ftpfs_remote_fs);
	  ftpfs_remote_fs = rfs;
	}
      break;

    case ARGP_KEY_SUCCESS:
      /* Validate the remote fs arg; at this point FTPFS_REMOTE_FS is in
	 SERVER:FS notation.  */
      if (state->arg_num == 0)
	argp_error (state, "No remote filesystem specified");
      else
	{
	  int h_err;		/* Host lookup error.  */
	  error_t err;
	  char *sep = strchr (ftpfs_remote_fs, '@');

	  if (sep)
	    /* FTPFS_REMOTE_FS includes a '@', which means that it's in
	       USER[:PASS]@HOST:FS notation, so we have to be careful not to
	       choose the wrong `:' as the SERVER-FS separator.  */
	    sep = strchr (sep, ':');
	  else
	    sep = strchr (ftpfs_remote_fs, ':');

	  if (! sep)
	    argp_error (state, "%s: No server specified", ftpfs_remote_fs);

	  ftpfs_remote_root = sep + 1;

	  /* Lookup the ftp server (the part before the `:').  */
	  *sep = '\0';
	  err = lookup_server (ftpfs_remote_fs, &ftpfs_ftp_params, &h_err);
	  if (err == EINVAL)
	    argp_failure (state, 10, 0, "%s: %s",
			  ftpfs_remote_fs, hstrerror (h_err));
	  else if (err)
	    argp_failure (state, 11, err, "%s", ftpfs_remote_fs);
	  *sep = ':';
	}

    case ARGP_KEY_INIT:
      /* Setup up state for our first child parser (common options).  */
      state->child_inputs[0] = &ftpfs_params;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

/* Runtime options.  */

static const struct argp_child runtime_argp_children[] =
  { {&common_argp}, {&netfs_std_runtime_argp}, {0} };
static struct argp runtime_argp =
  { 0, 0, 0, 0, runtime_argp_children };

/* Use by netfs_set_options to handle runtime option parsing.  */
struct argp *netfs_runtime_argp = &runtime_argp;

/* Return an argz string describing the current options.  Fill *ARGZ
   with a pointer to newly malloced storage holding the list and *LEN
   to the length of that storage.  */
error_t
netfs_append_args (char **argz, size_t *argz_len)
{
  char buf[80];
  error_t err = 0;

#define FOPT(fmt, arg) \
  do { \
    if (! err) \
      { \
	snprintf (buf, sizeof buf, fmt, arg); \
	err = argz_add (argz, argz_len, buf); \
      } \
  } while (0)

  if (ftpfs_ftp_hooks.cntl_debug)
    err = argz_add (argz, argz_len, "--debug");

  if (ftpfs->params.name_timeout != DEFAULT_NAME_TIMEOUT)
    FOPT ("--name-timeout=%d", ftpfs->params.name_timeout);
  if (ftpfs->params.stat_timeout != DEFAULT_STAT_TIMEOUT)
    FOPT ("--stat-timeout=%d", ftpfs->params.stat_timeout);
  if (ftpfs->params.node_cache_max != DEFAULT_NODE_CACHE_MAX)
    FOPT ("--node-cache-max=%d", ftpfs->params.node_cache_max);
  if (ftpfs->params.bulk_stat_period != DEFAULT_BULK_STAT_PERIOD)
    FOPT ("--bulk-stat-period=%d", ftpfs->params.bulk_stat_period);
  if (ftpfs->params.bulk_stat_threshold != DEFAULT_BULK_STAT_THRESHOLD)
    FOPT ("--bulk-stat-threshold=%d", ftpfs->params.bulk_stat_threshold);

  return argz_add (argz, argz_len, ftpfs_remote_fs);
}

/* Program entry point.  */
int 
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  const struct argp_child argp_children[] =
    { {&common_argp}, {&netfs_std_startup_argp}, {0} };
  struct argp argp =
    { startup_options, parse_startup_opt, args_doc, doc, argp_children };

  ftpfs_params.name_timeout = DEFAULT_NAME_TIMEOUT;
  ftpfs_params.stat_timeout = DEFAULT_STAT_TIMEOUT;
  ftpfs_params.node_cache_max = DEFAULT_NODE_CACHE_MAX;
  ftpfs_params.bulk_stat_period = DEFAULT_BULK_STAT_PERIOD;
  ftpfs_params.bulk_stat_threshold = DEFAULT_BULK_STAT_THRESHOLD;

  argp_parse (&argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);

  netfs_init ();

  err = maptime_map (0, 0, &ftpfs_maptime);
  if (err)
    error (3, err, "mapping time");

  err = ftpfs_create (ftpfs_remote_root, getpid (),
		      ftpfs_ftp_params, &ftpfs_ftp_hooks,
		      &ftpfs_params, &ftpfs);
  if (err)
    error (4, err, "%s", ftpfs_remote_fs);

  netfs_root_node = ftpfs->root;

  netfs_startup (bootstrap, 0);
  
  for (;;)
    netfs_server_loop ();
}
