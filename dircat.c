/* Hurd /proc filesystem, concatenation of two directories.
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

#include <stdlib.h>
#include <string.h>
#include "procfs.h"

struct dircat_node
{
  int num_dirs;
  struct node *dirs[0];
};

static error_t
dircat_get_contents (void *hook, char **contents, ssize_t *contents_len)
{
  struct dircat_node *dcn = hook;
  int i, sz, pos;
  error_t err;

  pos = 0;
  *contents = malloc (sz = 512);

  for (i=0; i < dcn->num_dirs; i++)
    {
      char *subcon;
      ssize_t sublen;

      /* Make sure we're not getting some old stuff.  */
      procfs_refresh (dcn->dirs[i]);

      err = procfs_get_contents (dcn->dirs[i], &subcon, &sublen);
      if (err)
        {
	  free (*contents);
	  *contents = NULL;
	  return err;
	}

      while (pos + sublen > sz)
	*contents = realloc (*contents, sz *= 2);

      memcpy (*contents + pos, subcon, sublen);
      pos += sublen;
    }

  *contents_len = pos;
  return 0;
}

static error_t
dircat_lookup (void *hook, const char *name, struct node **np)
{
  struct dircat_node *dcn = hook;
  error_t err;
  int i;

  err = ENOENT;
  for (i=0; err && i < dcn->num_dirs; i++)
    err = procfs_lookup (dcn->dirs[i], name, np);

  return err;
}

static void
dircat_release_dirs (struct node *const *dirs, int num_dirs)
{
  int i;

  for (i=0; i < num_dirs; i++)
    if (dirs[i])
      netfs_nrele (dirs[i]);
}

static void
dircat_cleanup (void *hook)
{
  struct dircat_node *dcn = hook;

  dircat_release_dirs (dcn->dirs, dcn->num_dirs);
  free (dcn);
}

struct node *
dircat_make_node (struct node *const *dirs, int num_dirs)
{
  static struct procfs_node_ops ops = {
    .get_contents = dircat_get_contents,
    .cleanup_contents = procfs_cleanup_contents_with_free,
    .lookup = dircat_lookup,
    .cleanup = dircat_cleanup,
  };
  struct dircat_node *dcn;
  int i;

  for (i=0; i < num_dirs; i++)
    if (! dirs[i])
      goto fail;

  dcn = malloc (sizeof *dcn + num_dirs * sizeof dcn->dirs[0]);
  if (! dcn)
    goto fail;

  dcn->num_dirs = num_dirs;
  memcpy (dcn->dirs, dirs, num_dirs * sizeof dcn->dirs[0]);
  return procfs_make_node (&ops, dcn);

fail:
  dircat_release_dirs (dirs, num_dirs);
  return NULL;
}

