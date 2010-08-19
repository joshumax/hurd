#include <stdlib.h>
#include <string.h>
#include "procfs.h"
#include "procfs_dir.h"

struct procfs_dir_node
{
  const struct procfs_dir_entry *entries;
  void *hook;
  void (*cleanup) (void *hook);
};

static error_t
procfs_dir_get_contents (void *hook, void **contents, size_t *contents_len)
{
  static const char dot_dotdot[] = ".\0..";
  struct procfs_dir_node *dn = hook;
  const struct procfs_dir_entry *ent;
  char *pos;

  *contents_len = sizeof dot_dotdot;
  for (ent = dn->entries; ent->name; ent++)
    *contents_len += strlen (ent->name) + 1;

  *contents = malloc (*contents_len);
  if (! *contents)
    return ENOMEM;

  memcpy (*contents, dot_dotdot, sizeof dot_dotdot);
  pos = *contents + sizeof dot_dotdot;
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

static void
procfs_dir_cleanup (void *hook)
{
  struct procfs_dir_node *dn = hook;

  if (dn->cleanup)
    dn->cleanup (dn->hook);

  free (dn);
}

struct node *
procfs_dir_make_node (const struct procfs_dir_entry *entries,
		      void *dir_hook, void (*cleanup) (void *dir_hook))
{
  static const struct procfs_node_ops ops = {
    .get_contents = procfs_dir_get_contents,
    .lookup = procfs_dir_lookup,
    .cleanup_contents = procfs_cleanup_contents_with_free,
    .cleanup = procfs_dir_cleanup,
  };
  struct procfs_dir_node *dn;

  dn = malloc (sizeof *dn);
  if (! dn)
    {
      if (cleanup)
	cleanup (dir_hook);

      return NULL;
    }

  dn->entries = entries;
  dn->hook = dir_hook;
  dn->cleanup = cleanup;

  return procfs_make_node (&ops, dn);
}

