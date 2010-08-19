#include <stdlib.h>
#include <string.h>
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
  void *contents;
  size_t contents_len;

  /* parent directory, if applicable */
  struct node *parent;
};

void
procfs_cleanup_contents_with_free (void *hook, void *cont, size_t len)
{
  free (cont);
}

void
procfs_cleanup_contents_with_vm_deallocate (void *hook, void *cont, size_t len)
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

  return np;

fail:
  if (ops->cleanup)
    ops->cleanup (hook);

  free (nn);
  return NULL;
}

/* FIXME: possibly not the fastest hash function... */
ino64_t
procfs_make_ino (struct node *np, const char *filename)
{
  unsigned short x[3];

  if (! strcmp (filename, "."))
    return np->nn_stat.st_ino;
  if (! strcmp (filename, ".."))
    return np->nn->parent ? np->nn->parent->nn_stat.st_ino : /* FIXME: */ 42;

  assert (sizeof np->nn_stat.st_ino > sizeof x);
  memcpy (x, &np->nn_stat.st_ino, sizeof x);

  while (*filename)
    {
      x[0] ^= *(filename++);
      jrand48 (x);
    }

  return jrand48 (x);
}

error_t procfs_get_contents (struct node *np, void **data, size_t *data_len)
{
  if (np->nn->ops->enable_refresh_hack_and_break_readdir && np->nn->contents)
    {
      if (np->nn->ops->cleanup_contents)
	np->nn->ops->cleanup_contents (np->nn->hook, np->nn->contents,
				       np->nn->contents_len);
      np->nn->contents = NULL;
    }

  if (! np->nn->contents && np->nn->ops->get_contents)
    {
      void *contents;
      size_t contents_len;
      error_t err;

      err = np->nn->ops->get_contents (np->nn->hook, &contents, &contents_len);
      if (err)
	return err;

      np->nn->contents = contents;
      np->nn->contents_len = contents_len;
    }

  *data = np->nn->contents;
  *data_len = np->nn->contents_len;
  return 0;
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
  if (np->nn->contents && np->nn->ops->cleanup_contents)
    np->nn->ops->cleanup_contents (np->nn->hook, np->nn->contents, np->nn->contents_len);

  if (np->nn->ops->cleanup)
    np->nn->ops->cleanup (np->nn->hook);

  if (np->nn->parent)
    netfs_nrele (np->nn->parent);

  free (np->nn);
}
