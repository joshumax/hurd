/* Hostmux leaf node functions

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
#include <argz.h>

#include "hostmux.h"

/* Read the contents of NODE (a symlink), for USER, into BUF. */
error_t
netfs_attempt_readlink (struct iouser *user, struct node *node, char *buf)
{
  assert (node->nn->name);
  memcpy (buf, node->nn->name->canon, node->nn_stat.st_size);
  touch (node, TOUCH_ATIME);
  return 0;
}

/* Append BUF, of length BUF_LEN to *TO, of length *TO_LEN, reallocating and
   updating *TO & *TO_LEN appropriately.  If an allocation error occurs,
   *TO's old value is freed, and *TO is set to 0.  */
static void
str_append (char **to, size_t *to_len, const char *buf, const size_t buf_len)
{
  size_t new_len = *to_len + buf_len;
  char *new_to = realloc (*to, new_len + 1);

  if (new_to)
    {
      memcpy (new_to + *to_len, buf, buf_len);
      new_to[new_len] = '\0';
      *to = new_to;
      *to_len = new_len;
    }
  else
    {
      free (*to);
      *to = 0;
    }
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
      char *arg = 0;
      int did_replace = 0;
      struct hostmux *mux = node->nn->mux;
      char *template = mux->trans_template;
      size_t template_len = mux->trans_template_len;
      char *host_pat = mux->host_pat;
      const char *host = node->nn->name->canon;
      size_t host_len = strlen (host);

      *argz = 0;			/* Initialize return value.  */
      *argz_len = 0;

      if (host_pat)
	{
	  size_t host_pat_len = strlen (host_pat);

	  while (!err && (arg = argz_next (template, template_len, arg)))
	    {
	      char *match = strstr (arg, host_pat);
	      if (match)
		{
		  char *from = match + host_pat_len;
		  size_t to_len = match - arg;
		  char *to = strndup (arg, to_len);

		  while (to && from)
		    {
		      str_append (&to, &to_len, host, host_len);
		      if (to)
			{
			  match = strstr (from, host_pat);
			  if (match)
			    {
			      str_append (&to, &to_len, from, match - from);
			      from = match + host_pat_len;
			    }
			  else
			    {
			      str_append (&to, &to_len, from, strlen (from));
			      from = 0;
			    }
			}
		    }

		  if (to)
		    {
		      err = argz_add (argz, argz_len, to);
		      free (to);
		    }
		  else
		    err = ENOMEM;

		  did_replace = 1;
		}
	      else
		err = argz_add (argz, argz_len, arg);
	    }
	}
      else
	err = argz_append (argz, argz_len, template, template_len);

      if (!err && !did_replace)
	err = argz_add (argz, argz_len, host);

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

  name->node = new;

  *node = new;

  return 0;
}
