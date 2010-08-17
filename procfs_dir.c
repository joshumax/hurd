#include <stdlib.h>
#include <string.h>
#include "procfs.h"
#include "procfs_dir.h"

struct procfs_dir_node
{
  const struct procfs_dir_entry *entries;
  void *hook;
};

static error_t
procfs_dir_get_contents (void *hook, void **contents, size_t *contents_len)
{
  struct procfs_dir_node *dn = hook;
  const struct procfs_dir_entry *ent;
  char *pos;

  *contents_len = 0;
  for (ent = dn->entries; ent->name; ent++)
    *contents_len += strlen (ent->name) + 1;

  *contents = malloc (*contents_len);
  if (! *contents)
    return ENOMEM;

  pos = *contents;
  for (ent = dn->entries; ent->name; ent++)
    {
      strcpy (pos, ent->name);
      pos += strlen (ent->name) + 1;
    }

  return 0;
}

static error_t
procfs_dir_lookup (void *hook, const char *name, struct node **np)
{
  struct procfs_dir_node *dn = hook;
  const struct procfs_dir_entry *ent;

  for (ent = dn->entries; ent->name && strcmp (name, ent->name); ent++);
  if (! ent->name)
    return ENOENT;

  *np = ent->make_node (dn->hook, ent->hook);
  if (! *np)
    return ENOMEM;

  return 0;
}

struct node *
procfs_dir_make_node (const struct procfs_dir_entry *entries, void *dir_hook)
{
  static const struct procfs_node_ops ops = {
    .get_contents = procfs_dir_get_contents,
    .lookup = procfs_dir_lookup,
    .cleanup_contents = free,
    .cleanup = free,
  };
  struct procfs_dir_node *dn;
  struct node *np;

  dn = malloc (sizeof *dn);
  if (! dn)
    return NULL;

  dn->entries = entries;
  dn->hook = dir_hook;

  np = procfs_make_node (&ops, dn);
  if (! np)
    free (dn);

  return np;
}

