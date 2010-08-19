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
     or a derived function.  Note that the parent will be kept alive as
     long as the child exists, so you can safely reference the parent's
     data from the child.  You may want to consider locking if there's
     any mutation going on, though.  */
  error_t (*lookup) (void *hook, const char *name, struct node **np);

  /* Destroy this node.  */
  void (*cleanup) (void *hook);

  /* FIXME: This is needed because the root node is persistent, and we
     want the list of processes to be updated. However, this means that
     readdir() on the root node runs the risk of returning incoherent
     results if done in multiple runs and processes are added/removed in
     the meantime.  The right way to fix this is probably to add a
     getroot() user hook function to libnetfs.  */
  int enable_refresh_hack_and_break_readdir;
};

/* These helper functions can be used as procfs_node_ops.cleanup_contents. */
void procfs_cleanup_contents_with_free (void *, void *, size_t);
void procfs_cleanup_contents_with_vm_deallocate (void *, void *, size_t);

/* Create a new node and return it.  Returns NULL if it fails to allocate
   enough memory.  In this case, ops->cleanup will be invoked.  */
struct node *procfs_make_node (const struct procfs_node_ops *ops, void *hook);


/* Interface for the libnetfs side. */

/* Get the inode number which will be given to a child of NP named FILENAME.
   This allows us to retreive them for readdir() without creating the
   corresponding child nodes.  */
ino64_t procfs_make_ino (struct node *np, const char *filename);

error_t procfs_get_contents (struct node *np, void **data, size_t *data_len);
error_t procfs_lookup (struct node *np, const char *name, struct node **npp);
void procfs_cleanup (struct node *np);

