#include <stdlib.h>
#include <string.h>
#include "procfs.h"
#include "procfs_dir.h"

struct procfs_dir_node
{
  const struct procfs_dir_ops *ops;
  void *hook;
};

static int
entry_exists (struct procfs_dir_node *dir, const struct procfs_dir_entry *ent)
{
  if (ent->ops.exists)
    return ent->ops.exists (dir->hook, ent->hook);
  if (dir->ops->entry_ops.exists)
    return dir->ops->entry_ops.exists (dir->hook, ent->hook);

  return 1;
}

static error_t
procfs_dir_get_contents (void *hook, char **contents, ssize_t *contents_len)
{
  static const char dot_dotdot[] = ".\0..";
  struct procfs_dir_node *dir = hook;
  const struct procfs_dir_entry *ent;
  int pos;

  /* Evaluate how much space is needed.  Note that we include the hidden
     entries, just in case their status changes between now and then.  */
  pos = sizeof dot_dotdot;
  for (ent = dir->ops->entries; ent->name; ent++)
    pos += strlen (ent->name) + 1;

  *contents = malloc (pos);
  if (! *contents)
    return ENOMEM;

  memcpy (*contents, dot_dotdot, sizeof dot_dotdot);
  pos = sizeof dot_dotdot;
  for (ent = dir->ops->entries; ent->name; ent++)
    {
      if (! entry_exists (dir, ent))
	continue;

      strcpy (*contents + pos, ent->name);
      pos += strlen (ent->name) + 1;
    }

  *contents_len = pos;
  return 0;
}

static error_t
procfs_dir_lookup (void *hook, const char *name, struct node **np)
{
  struct procfs_dir_node *dir = hook;
  const struct procfs_dir_entry *ent;

  for (ent = dir->ops->entries; ent->name && strcmp (name, ent->name); ent++);
  if (! ent->name)
    return ENOENT;

  if (ent->ops.make_node)
    *np = ent->ops.make_node (dir->hook, ent->hook);
  else if (dir->ops->entry_ops.make_node)
    *np = dir->ops->entry_ops.make_node (dir->hook, ent->hook);
  else
    return EGRATUITOUS;

  if (! *np)
    return ENOMEM;

  return 0;
}

static void
procfs_dir_cleanup (void *hook)
{
  struct procfs_dir_node *dir = hook;

  if (dir->ops->cleanup)
    dir->ops->cleanup (dir->hook);

  free (dir);
}

struct node *
procfs_dir_make_node (const struct procfs_dir_ops *dir_ops, void *dir_hook)
{
  static const struct procfs_node_ops ops = {
    .get_contents = procfs_dir_get_contents,
    .lookup = procfs_dir_lookup,
    .cleanup_contents = procfs_cleanup_contents_with_free,
    .cleanup = procfs_dir_cleanup,
  };
  struct procfs_dir_node *dir;

  dir = malloc (sizeof *dir);
  if (! dir)
    {
      if (dir_ops->cleanup)
	dir_ops->cleanup (dir_hook);

      return NULL;
    }

  dir->ops = dir_ops;
  dir->hook = dir_hook;

  return procfs_make_node (&ops, dir);
}

