/* Hurd /proc filesystem, basic infrastructure.
   Copyright (C) 2010 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

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
     names of the entries.  If upon return, *CONTENTS_LEN is negative or
     unchanged, the call is considered to have failed because of a memory
     allocation error.  */
  error_t (*get_contents) (void *hook, char **contents, ssize_t *contents_len);
  void (*cleanup_contents) (void *hook, char *contents, ssize_t contents_len);

  /* Lookup NAME in this directory, and store the result in *np.  The
     returned node should be created by lookup() using procfs_make_node() 
     or a derived function.  Note that the parent will be kept alive as
     long as the child exists, so you can safely reference the parent's
     data from the child.  You may want to consider locking if there's
     any mutation going on, though.  */
  error_t (*lookup) (void *hook, const char *name, struct node **np);

  /* Destroy this node.  */
  void (*cleanup) (void *hook);

  /* Get the passive translator record.  */
  error_t (*get_translator) (void *hook, char **argz, size_t *argz_len);
};

/* These helper functions can be used as procfs_node_ops.cleanup_contents. */
void procfs_cleanup_contents_with_free (void *, char *, ssize_t);
void procfs_cleanup_contents_with_vm_deallocate (void *, char *, ssize_t);

/* Create a new node and return it.  Returns NULL if it fails to allocate
   enough memory.  In this case, ops->cleanup will be invoked.  */
struct node *procfs_make_node (const struct procfs_node_ops *ops, void *hook);

/* Set the owner of the node NP.  Must be called right after the node
   has been created.  */
void procfs_node_chown (struct node *np, uid_t owner);

/* Set the permission bits of the node NP.  Must be called right after
   the node has been created.  */
void procfs_node_chmod (struct node *np, mode_t mode);

/* Set the type of the node NP.  If type is S_IFLNK, appropriate
   permission bits will be set as well.  Must be called right after the
   node has been created.  */
void procfs_node_chtype (struct node *np, mode_t type);


/* Interface for the libnetfs side. */

/* Get the inode number which will be given to a child of NP named FILENAME.
   This allows us to retrieve them for readdir() without creating the
   corresponding child nodes.  */
ino64_t procfs_make_ino (struct node *np, const char *filename);

/* Forget the current cached contents for the node.  This is done before reads
   from offset 0, to ensure that the data are recent even for utilities such as
   top which keep some nodes open.  */
void procfs_refresh (struct node *np);

error_t procfs_get_contents (struct node *np, char **data, ssize_t *data_len);
error_t procfs_lookup (struct node *np, const char *name, struct node **npp);
void procfs_cleanup (struct node *np);

/* Get the passive translator record if any.  */
error_t procfs_get_translator (struct node *np, char **argz, size_t *argz_len);

