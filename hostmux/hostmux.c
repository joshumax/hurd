/* Multiplexing filesystems by host

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

#include <unistd.h>
#include <argp.h>
#include <argz.h>
#include <error.h>
#include <sys/time.h>

#include "hostmux.h"

int netfs_maxsymlinks = 25;

volatile struct mapped_time_value *hostmux_mapped_time;

/* Startup options.  */
static const struct argp_option options[] =
{
  { "host-pattern", 'H', "PAT", OPTION_ARG_OPTIONAL,
    "The string to replace in the translator specification with the hostname;"
    " if empty, or doesn't occur, the hostname is appended as additional"
    " argument instead (default `${host}')" },
  { 0 }
};
static const char args_doc[] = "TRANSLATOR [ARG...]";
static const char doc[] =
  "A translator for invoking host-specific translators"
  "\vThis translator appears like a directory in which hostnames can be"
  " looked up, and will start TRANSLATOR to service each resulting node.";

/* NFS client main program */
int
main (int argc, char **argv)
{
  error_t err;
  struct stat ul_stat;
  mach_port_t bootstrap;
  struct hostmux mux = { host_pat: "${host}", next_fileno: 10 };
  struct netnode root_nn = { mux: &mux };

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'H':
	  mux.host_pat = arg; break;
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
  argp_parse (&argp, argc, argv, 0, 0, 0);
    
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  netfs_init ();

  /* Create the root node (some attributes initialized below).  */
  netfs_root_node = netfs_make_node (&root_nn);
  if (! netfs_root_node)
    error (5, ENOMEM, "Cannot create root node");

  err = maptime_map (0, 0, &hostmux_mapped_time);
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
  touch (netfs_root_node, TOUCH_ATIME|TOUCH_MTIME|TOUCH_CTIME);
  
  for (;;)			/* ?? */
    netfs_server_loop ();
}
