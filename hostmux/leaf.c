/* Hostmux leaf node functions

   Copyright (C) 1997,2002 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <string.h>
#include <argz.h>

#include "hostmux.h"

/* Read the contents of NODE (a symlink), for USER, into BUF. */
error_t
netfs_attempt_readlink (struct iouser *user, struct node *node, char *buf)
{
  assert_backtrace (node->nn->name);
  memcpy (buf, node->nn->name->canon, node->nn_stat.st_size);
  fshelp_touch (&node->nn_stat, TOUCH_ATIME, hostmux_maptime);
  return 0;
}

/* For locked node NODE with S_IPTRANS set in its mode, look up the name of
   its translator.  Store the name into newly malloced storage, and return it
   in *ARGZ; set *ARGZ_LEN to the total length.

   For hostmux, this creates a new translator string by instantiating the
   global translator template.  */
error_t
netfs_get_translator (struct node *node, char **argz, size_t *argz_len)
{
  if (! node->nn->name)
    return EINVAL;
  else
    {
      error_t err = 0;
      unsigned replace_count = 0;
      struct hostmux *mux = node->nn->mux;

      *argz = 0;			/* Initialize return value.  */
      *argz_len = 0;

      /* Return a copy of MUX's translator template, with occurrences of
	 HOST_PAT replaced by the canonical hostname.  */
      err = argz_append (argz, argz_len,
			 mux->trans_template, mux->trans_template_len);
      if (! err)
	err = argz_replace (argz, argz_len,
			    mux->host_pat, node->nn->name->canon,
			    &replace_count);

      if (!err && replace_count == 0)
	/* Default, if no instances of HOST_PAT occur, is to append the
	   hostname. */
	err = argz_add (argz, argz_len, node->nn->name->canon);

      if (err && *argz_len > 0)
	free (*argz);

      return err;
    }
}

/* Create a new leaf node in MUX, with a name NAME, and return the new node
   with a single reference in NODE.  */
error_t
create_host_node (struct hostmux *mux, struct hostmux_name *name,
		  struct node **node)
{
  struct node *new;
  struct netnode *nn = malloc (sizeof (struct netnode));

  if (! nn)
    return ENOMEM;

  nn->mux = mux;
  nn->name = name;

  new = netfs_make_node (nn);
  if (! new)
    {
      free (nn);
      return ENOMEM;
    }

  new->nn_stat = mux->stat_template;
  new->nn_stat.st_ino = name->fileno;

  if (strcmp (name->name, name->canon) == 0)
    /* The real name of the host, make a real node.  */
    {
      new->nn_stat.st_mode = (S_IFREG | S_IPTRANS | 0666);
      new->nn_stat.st_size = 0;
    }
  else
    /* An alias for this host, make a symlink instead.  */
    {
      new->nn_stat.st_mode = (S_IFLNK | 0666);
      new->nn_stat.st_size = strlen (name->canon);
    }
  new->nn_translated = new->nn_stat.st_mode;

  fshelp_touch (&new->nn_stat, TOUCH_ATIME|TOUCH_MTIME|TOUCH_CTIME,
		hostmux_maptime);

  name->node = new;

  *node = new;

  return 0;
}
