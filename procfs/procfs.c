/* Hurd /proc filesystem, basic infrastructure.
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
#include <unistd.h>
#include <fcntl.h>
#include <mach.h>
#include <hurd/netfs.h>
#include <hurd/fshelp.h>
#include "procfs.h"

struct netnode
{
  const struct procfs_node_ops *ops;
  void *hook;

  /* (cached) contents of the node */
  char *contents;
  ssize_t contents_len;

  /* parent directory, if applicable */
  struct node *parent;
};

void
procfs_cleanup_contents_with_free (void *hook, char *cont, ssize_t len)
{
  free (cont);
}

void
procfs_cleanup_contents_with_vm_deallocate (void *hook, char *cont, ssize_t len)
{
  vm_deallocate (mach_task_self (), (vm_address_t) cont, (vm_size_t) len);
}

struct node *procfs_make_node (const struct procfs_node_ops *ops, void *hook)
{
  struct netnode *nn;
  struct node *np;
 
  nn = malloc (sizeof *nn);
  if (! nn)
    goto fail;

  memset (nn, 0, sizeof *nn);
  nn->ops = ops;
  nn->hook = hook;

  np = netfs_make_node (nn);
  if (! np)
    goto fail;

  np->nn = nn;
  memset (&np->nn_stat, 0, sizeof np->nn_stat);
  np->nn_translated = 0;

  if (np->nn->ops->lookup)
    np->nn_stat.st_mode = S_IFDIR | 0555;
  else
    np->nn_stat.st_mode = S_IFREG | 0444;

  np->nn_stat.st_uid = getuid ();
  np->nn_stat.st_gid = getgid ();

  return np;

fail:
  if (ops->cleanup)
    ops->cleanup (hook);

  free (nn);
  return NULL;
}

void procfs_node_chown (struct node *np, uid_t owner)
{
  np->nn_stat.st_uid = owner;
}

void procfs_node_chmod (struct node *np, mode_t mode)
{
  np->nn_stat.st_mode = (np->nn_stat.st_mode & ~ALLPERMS) | mode;
  np->nn_translated = np->nn_stat.st_mode;
}

void procfs_node_chtype (struct node *np, mode_t type)
{
  np->nn_stat.st_mode = (np->nn_stat.st_mode & ~S_IFMT) | type;
  np->nn_translated = np->nn_stat.st_mode;
  if (type == S_IFLNK)
    procfs_node_chmod (np, 0777);
}

/* FIXME: possibly not the fastest hash function... */
ino64_t
procfs_make_ino (struct node *np, const char *filename)
{
  unsigned short x[3];

  if (! strcmp (filename, "."))
    return np->nn_stat.st_ino;
  if (! strcmp (filename, ".."))
    return np->nn->parent ? np->nn->parent->nn_stat.st_ino : /* FIXME: */ 2;

  assert_backtrace (sizeof np->nn_stat.st_ino > sizeof x);
  memcpy (x, &np->nn_stat.st_ino, sizeof x);

  while (*filename)
    {
      x[0] ^= *(filename++);
      jrand48 (x);
    }

  return (unsigned long) jrand48 (x);
}

error_t procfs_get_contents (struct node *np, char **data, ssize_t *data_len)
{
  if (! np->nn->contents && np->nn->ops->get_contents)
    {
      char *contents;
      ssize_t contents_len;
      error_t err;

      contents_len = -1;
      err = np->nn->ops->get_contents (np->nn->hook, &contents, &contents_len);
      if (err)
	return err;
      if (contents_len < 0)
	return ENOMEM;

      np->nn->contents = contents;
      np->nn->contents_len = contents_len;
    }

  *data = np->nn->contents;
  *data_len = np->nn->contents_len;
  return 0;
}

void procfs_refresh (struct node *np)
{
  if (np->nn->contents && np->nn->ops->cleanup_contents)
    np->nn->ops->cleanup_contents (np->nn->hook, np->nn->contents, np->nn->contents_len);

  np->nn->contents = NULL;
}

error_t procfs_lookup (struct node *np, const char *name, struct node **npp)
{
  error_t err = ENOENT;

  if (err && ! strcmp (name, "."))
    {
      netfs_nref(*npp = np);
      err = 0;
    }

  if (err && np->nn->parent && ! strcmp (name, ".."))
    {
      netfs_nref(*npp = np->nn->parent);
      err = 0;
    }

  if (err && np->nn->ops->lookup)
    {
      err = np->nn->ops->lookup (np->nn->hook, name, npp);
      if (! err)
        {
	  (*npp)->nn_stat.st_ino = procfs_make_ino (np, name);
	  netfs_nref ((*npp)->nn->parent = np);
	}
    }

  return err;
}

void procfs_cleanup (struct node *np)
{
  procfs_refresh (np);

  if (np->nn->ops->cleanup)
    np->nn->ops->cleanup (np->nn->hook);

  if (np->nn->parent)
    netfs_nrele (np->nn->parent);

  free (np->nn);
}

error_t procfs_get_translator (struct node *np,
                               char **argz,
                               size_t *argz_len)
{
  if (np->nn->ops->get_translator)
    return np->nn->ops->get_translator (np->nn->hook, argz, argz_len);

  *argz = NULL;
  *argz_len = 0;
  return 0;
}
