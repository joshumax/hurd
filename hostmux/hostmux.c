/* Multiplexing filesystems by host

   Copyright (C) 1997, 2002 Free Software Foundation, Inc.
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

#include <unistd.h>
#include <argp.h>
#include <argz.h>
#include <error.h>
#include <sys/time.h>

#include <version.h>

#include "hostmux.h"

const char *argp_program_version = STANDARD_HURD_VERSION (hostmux);

char *netfs_server_name = "hostmux";
char *netfs_server_version = HURD_VERSION;
int netfs_maxsymlinks = 25;

volatile struct mapped_time_value *hostmux_mapped_time;

#define DEFAULT_HOST_PAT "${host}"

/* Startup options.  */
static const struct argp_option options[] =
{
  { "host-pattern", 'H', "PAT", 0,
    "The string to replace in the translator specification with the hostname;"
    " if empty, or doesn't occur, the hostname is appended as additional"
    " argument instead (default `" DEFAULT_HOST_PAT "')" },
  { "canonicalize", 'C', 0, 0,
    "Canonicalize hostname before passing it to TRANSLATOR, aliases will"
    " show up as symbolic links to the canonicalized entry" },
  { 0 }
};
static const char args_doc[] = "TRANSLATOR [ARG...]";
static const char doc[] =
  "A translator for invoking host-specific translators."
  "\vThis translator appears like a directory in which hostnames can be"
  " looked up, and will start TRANSLATOR to service each resulting node.";

/* Return an argz string describing the current options.  Fill *ARGZ
   with a pointer to newly malloced storage holding the list and *LEN
   to the length of that storage.  */
error_t
netfs_append_args (char **argz, size_t *argz_len)
{
  char buf[80];
  error_t err = 0;
  struct hostmux *mux = netfs_root_node->nn->mux;

#define FOPT(fmt, arg) \
  do { \
    if (! err) \
      { \
	snprintf (buf, sizeof buf, fmt, arg); \
	err = argz_add (argz, argz_len, buf); \
      } \
  } while (0)

  if (strcmp (mux->host_pat, DEFAULT_HOST_PAT) != 0)
    FOPT ("--host-pattern=%s", mux->host_pat);

  if (! err)
    err = argz_append (argz, argz_len,
		       mux->trans_template, mux->trans_template_len);

  return err;
}

int
main (int argc, char **argv)
{
  error_t err;
  struct stat ul_stat;
  mach_port_t bootstrap;
  struct hostmux mux = { host_pat: DEFAULT_HOST_PAT, next_fileno: 10 };
  struct netnode root_nn = { mux: &mux };

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'H':
	  mux.host_pat = arg; break;
	case 'C':
	  mux.canonicalize = 1; break;
	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);
	case ARGP_KEY_ARGS:
	  /* Steal the entire tail of arg vector for our own use.  */
	  return argz_create (state->argv + state->next,
			      &mux.trans_template, &mux.trans_template_len);
	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = { options, parse_opt, args_doc, doc };

  /* Parse our command line arguments.  */
  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  netfs_init ();

  /* Create the root node (some attributes initialized below).  */
  netfs_root_node = netfs_make_node (&root_nn);
  if (! netfs_root_node)
    error (5, ENOMEM, "Cannot create root node");

  err = maptime_map (0, 0, &hostmux_maptime);
  if (err)
    error (6, err, "Cannot map time");

  /* Handshake with the party trying to start the translator.  */
  mux.underlying = netfs_startup (bootstrap, 0);

  /* We inherit various attributes from the node underlying this translator. */
  err = io_stat (mux.underlying, &ul_stat);
  if (err)
    error (7, err, "Cannot stat underlying node");

  /* MUX.stat_template contains some fields that are inherited by all nodes
     we create.  */
  mux.stat_template.st_uid = ul_stat.st_uid;
  mux.stat_template.st_gid = ul_stat.st_gid;
  mux.stat_template.st_author = ul_stat.st_author;
  mux.stat_template.st_fsid = getpid ();
  mux.stat_template.st_nlink = 1;
  mux.stat_template.st_fstype = FSTYPE_MISC;

  /* Initialize the root node's stat information.  */
  netfs_root_node->nn_stat = mux.stat_template;
  netfs_root_node->nn_stat.st_ino = 2;
  netfs_root_node->nn_stat.st_mode =
    S_IFDIR | (ul_stat.st_mode & ~S_IFMT & ~S_ITRANS);
  netfs_root_node->nn_translated = 0;

  /* If the underlying node isn't a directory, propagate read permission to
     execute permission since we need that for lookups.  */
  if (! S_ISDIR (ul_stat.st_mode))
    {
      if (ul_stat.st_mode & S_IRUSR)
	netfs_root_node->nn_stat.st_mode |= S_IXUSR;
      if (ul_stat.st_mode & S_IRGRP)
	netfs_root_node->nn_stat.st_mode |= S_IXGRP;
      if (ul_stat.st_mode & S_IROTH)
	netfs_root_node->nn_stat.st_mode |= S_IXOTH;
    }

  fshelp_touch (&netfs_root_node->nn_stat, TOUCH_ATIME|TOUCH_MTIME|TOUCH_CTIME,
		hostmux_maptime);

  for (;;)			/* ?? */
    netfs_server_loop ();
}
