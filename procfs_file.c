#include <hurd/hurd_types.h>
#include <stdlib.h>
#include <string.h>
#include "procfs.h"
#include "procfs_file.h"

struct procfs_file
{
  void *contents;
  size_t len;
  void (*cleanup)(void *contents);
};

error_t
procfs_file_getcontents (void *hook, void **contents, size_t *contents_len)
{
  struct procfs_file *f = hook;

  *contents = f->contents;
  *contents_len = f->len;
  return 0;
}

void
procfs_file_cleanup (void *hook)
{
  struct procfs_file *f = hook;

  if (f->cleanup)
    f->cleanup (f->contents);

  free (f);
}

struct node *
procfs_file_make_node (void *contents, ssize_t len, void (*cleanup)(void *))
{
  static const struct procfs_node_ops ops = {
    .get_contents = procfs_file_getcontents,
    .cleanup = procfs_file_cleanup,
  };
  struct procfs_file *f;
  struct node *np;

  f = malloc (sizeof *f);
  if (! f)
    return NULL;

  f->contents = contents;
  f->len = (len >= 0) ? len : strlen (f->contents);
  f->cleanup = cleanup;

  np = procfs_make_node (&ops, f);
  if (! np)
    free (f);

  return np;
}

