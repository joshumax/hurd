/* Set options in a running filesystem

   Copyright (C) 1995,96,97,98,2002,2004 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>
#include <fcntl.h>
#include <unistd.h>

#include <error.h>
#include <argz.h>
#include <version.h>

#include <hurd/fsys.h>

const char *argp_program_version = STANDARD_HURD_VERSION (fsysopts);

static struct argp_option options[] =
{
  {"dereference", 'L', 0, 0, "If FILESYS is a symbolic link, follow it"},
  {"recursive",   'R', 0, 0, "Pass these options to any child translators"},
  {0, 0, 0, 0}
};
static char *args_doc = "FILESYS [FS_OPTION...]";
static char *doc = "Get or set command line options for running translator FILESYS."
"\vThe legal values for FS_OPTION depends on FILESYS, but\
 some common ones are: --readonly, --writable, --update, --sync[=INTERVAL],\
 and --nosync.\n\nIf no options are supplied, FILESYS's existing options\
 are printed";

/* ---------------------------------------------------------------- */

int
main(int argc, char *argv[])
{
  error_t err;

  /* The file we use as a handle to get FSYS.  */
  char *node_name = 0;
  file_t node;

  /* The filesystem options vector, in '\0' separated format.  */
  char *argz = 0;
  size_t argz_len = 0;

  int deref = 0, recursive = 0;

  /* Parse a command line option.  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:
	  node_name = arg;
	  err = argz_create (state->argv + state->next, &argz, &argz_len);
	  if (err)
	    error(3, err, "Can't create options vector");
	  state->next = state->argc; /* stop parsing */
	  break;

	case 'R': recursive = 1; break;
	case 'L': deref = 1; break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);
	  return EINVAL;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }

  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  node = file_name_lookup (node_name, (deref ? 0 : O_NOLINK), 0666);
  if (node == MACH_PORT_NULL)
    error (1, errno, "%s", node_name);

  if (argz_len)
    {
      /* The filesystem we're passing options to.  */
      fsys_t fsys;

      /* Get the filesystem for NODE.  */
      err = file_getcontrol (node, &fsys);
      if (err)
	error (2, err, "%s", node_name);

      err = fsys_set_options (fsys, argz, argz_len, recursive);
      if (err)
	{
	  argz_stringify (argz, argz_len, ' ');
	  error(5, err, "%s: %s", node_name, argz);
	}
    }
  else
    {
      err = file_get_fs_options (node, &argz, &argz_len);
      if (err)
	error (5, err, "%s", node_name);
      argz_stringify (argz, argz_len, ' ');
      puts (argz);
    }

  return 0;
}
