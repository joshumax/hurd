/* Parse standard run-time options

   Copyright (C) 1995, 1996, 1997, 1998, 1999 Free Software Foundation, Inc.

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

#include <argp.h>

#include "priv.h"

static const struct argp_option
std_runtime_options[] =
{
  {"update", 'u',  0, 0, "Flush any meta-data cached in core"},
  {"remount", 0, 0, OPTION_HIDDEN | OPTION_ALIAS}, /* deprecated */
  {0, 0}
};

struct parse_hook
{
  int readonly, sync, sync_interval, remount, nosuid, noexec, noatime,
    noinheritdirgroup;
};

/* Implement the options in H, and free H.  */
static error_t
set_opts (struct parse_hook *h)
{
  error_t err = 0;

  /* Do things in this order: remount, change readonly, change-sync; always
     do the remount while the disk is readonly, even if only temporarily.  */

  if (h->remount)
    {
      /* We can only remount while readonly.  */
      err = diskfs_set_readonly (1);
      if (!err)
	err = diskfs_remount ();
    }

  if (h->readonly != diskfs_readonly)
    {
      if (err)
	diskfs_set_readonly (h->readonly); /* keep the old error.  */
      else
	err = diskfs_set_readonly (h->readonly);
    }

  /* Change sync mode.  */
  if (h->sync)
    {
      diskfs_synchronous = 1;
      diskfs_set_sync_interval (0); /* Don't waste time syncing.  */
    }
  else
    {
      diskfs_synchronous = 0;
      if (h->sync_interval >= 0)
	diskfs_set_sync_interval (h->sync_interval);
    }

  if (h->nosuid != -1)
    _diskfs_nosuid = h->nosuid;
  if (h->noexec != -1)
    _diskfs_noexec = h->noexec;
  if (h->noatime != -1)
    _diskfs_noatime = h->noatime;
  if (h->noinheritdirgroup != -1)
    _diskfs_no_inherit_dir_group = h->noinheritdirgroup;

  free (h);

  return err;
}

/* Parse diskfs standard runtime options.  */
static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  struct parse_hook *h = state->hook;
  switch (opt)
    {
    case 'r': h->readonly = 1; break;
    case 'w': h->readonly = 0; break;
    case 'u': h->remount = 1; break;
    case 'S': h->nosuid = 1; break;
    case 'E': h->noexec = 1; break;
    case 'A': h->noatime = 1; break;
    case OPT_SUID_OK: h->nosuid = 0; break;
    case OPT_EXEC_OK: h->noexec = 0; break;
    case OPT_ATIME: h->noatime = 0; break;
    case OPT_NO_INHERIT_DIR_GROUP: h->noinheritdirgroup = 1; break;
    case OPT_INHERIT_DIR_GROUP: h->noinheritdirgroup = 0; break;
    case 'n': h->sync_interval = 0; h->sync = 0; break;
    case 's':
      if (arg)
	{
	  h->sync = 0;
	  h->sync_interval = atoi (arg);
	}
      else
	h->sync = 1;
      break;

    case ARGP_KEY_INIT:
      if (state->input)
	state->hook = state->input; /* Share hook with parent.  */
      else
	{
	  h = state->hook = malloc (sizeof (struct parse_hook));
	  if (! h)
	    return ENOMEM;
	  h->readonly = diskfs_readonly;
	  h->sync = diskfs_synchronous;
	  h->sync_interval = -1;
	  h->remount = 0;
	  h->nosuid = h->noexec = h->noatime = h->noinheritdirgroup = -1;

	  /* We know that we have one child, with which we share our hook.  */
	  state->child_inputs[0] = h;
	}
      break;

    case ARGP_KEY_ERROR:
      if (! state->input)
	free (h);
      break;

    case ARGP_KEY_SUCCESS:
      if (! state->input)
	return set_opts (h);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp common_argp = { diskfs_common_options, parse_opt };

static const struct argp_child children[] = { {&common_argp}, {0} };
const struct argp diskfs_std_runtime_argp =
{
  std_runtime_options, parse_opt, 0, 0, children
};
