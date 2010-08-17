#include <hurd/hurd_types.h>
#include <hurd/netfs.h>


/* Interface for the procfs side. */

/* Any of these callback functions can be omitted, in which case
   reasonable defaults will be used.  The initial file mode and type
   depend on whether a lookup function is provided, but can be
   overridden in update_stat().  */
struct procfs_node_ops
{
  /* Fetch the contents of a node.  A pointer to the contents should be
     returned in *CONTENTS and their length in *CONTENTS_LEN.  The exact
     nature of these data depends on whether the node is a regular file,
     symlink or directory, as determined by the file mode in
     netnode->nn_stat.  For regular files and symlinks, they are what
     you would expect; for directories, they are an argz vector of the
     names of the entries.  */
  error_t (*get_contents) (void *hook, void **contents, size_t *contents_len);
  void (*cleanup_contents) (void *hook, void *contents, size_t contents_len);

  /* Lookup NAME in this directory, and store the result in *np.  The
     returned node should be created by lookup() using procfs_make_node() 
     or a derived function.  */
  error_t (*lookup) (void *hook, const char *name, struct node **np);

  /* Destroy this node.  */
  void (*cleanup) (void *hook);
};

/* These helper functions can be used as procfs_node_ops.cleanup_contents. */
void procfs_cleanup_contents_with_free (void *, void *, size_t);
void procfs_cleanup_contents_with_vm_deallocate (void *, void *, size_t);

/* Create a new node and return it.  Returns NULL if it fails to allocate
   enough memory.  In this case, ops->cleanup will be invoked.  */
struct node *procfs_make_node (const struct procfs_node_ops *ops, void *hook);


/* Interface for the libnetfs side. */

error_t procfs_get_contents (struct node *np, void **data, size_t *data_len);
error_t procfs_lookup (struct node *np, const char *name, struct node **npp);
void procfs_cleanup (struct node *np);

