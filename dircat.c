#include <stdlib.h>
#include <string.h>
#include "procfs.h"

struct dircat_node
{
  struct node **dirs;
};

static error_t
dircat_get_contents (void *hook, char **contents, ssize_t *contents_len)
{
  struct dircat_node *dcn = hook;
  int i, sz, pos;
  error_t err;

  pos = 0;
  *contents = malloc (sz = 512);

  for (i=0; dcn->dirs[i]; i++)
    {
      char *subcon;
      ssize_t sublen;

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
  for (i=0; err && dcn->dirs[i]; i++)
    err = procfs_lookup (dcn->dirs[i], name, np);

  return err;
}

static void
dircat_release_dirs (struct node **dirs)
{
  int i;

  for (i=0; dirs[i]; i++)
    netfs_nrele (dirs[i]);
}

static void
dircat_cleanup (void *hook)
{
  struct dircat_node *dcn = hook;

  dircat_release_dirs (dcn->dirs);
  free (dcn);
}

struct node *
dircat_make_node (struct node **dirs)
{
  static struct procfs_node_ops ops = {
    .get_contents = dircat_get_contents,
    .cleanup_contents = procfs_cleanup_contents_with_free,
    .lookup = dircat_lookup,
    .cleanup = dircat_cleanup,
    /* necessary so that it propagates to proclist */
    .enable_refresh_hack_and_break_readdir = 1,
  };
  struct dircat_node *dcn;

  dcn = malloc (sizeof *dcn);
  if (! dcn)
    {
      dircat_release_dirs (dirs);
      return NULL;
    }

  dcn->dirs = dirs;
  return procfs_make_node (&ops, dcn);
}

