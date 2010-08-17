
/* Each entry associates a name with a callback function for creating new
   nodes corresponding to that entry.  */
struct procfs_dir_entry
{
  const char *name;
  struct node *(*make_node)(void *dir_hook, void *entry_hook);
  void *hook;
};

/* A simple directory is built from a table of entries.  The table is
   terminated by a null NAME pointer.  */
struct node *
procfs_dir_make_node (const struct procfs_dir_entry *entries, void *dir_hook);

