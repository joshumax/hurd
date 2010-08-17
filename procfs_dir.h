
/* Each entry associates a name with a callback function for creating new
   nodes corresponding to that entry.  */
struct procfs_dir_entry
{
  const char *name;
  struct node *(*make_node)(void *dir_hook, void *entry_hook);
  void *hook;
};

/* A simple directory is built from a table of entries.  The table is
   terminated by a null NAME pointer.  The DIR_HOOK is passed the
   MAKE_NODE callback function of looked up procfs_dir_entries, and to
   the provided CLEANUP function when the directory is destroyed.
   Returns the new directory node.  If not enough memory can be
   allocated, CLEANUP is invoked immediately and NULL is returned.  */
struct node *
procfs_dir_make_node (const struct procfs_dir_entry *entries,
		      void *dir_hook, void (*cleanup) (void *dir_hook));

