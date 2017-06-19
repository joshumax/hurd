/* Hierarchial filesystem support

   Copyright (C) 1995, 2002 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

#ifndef __TREEFS_H__
#define __TREEFS_H__

#include <errno.h>
#include <pthread.h>
#include <assert-backtrace.h>
#include <features.h>

#include <sys/stat.h>

#include <hurd/hurd_types.h>
#include <hurd/ports.h>
#include <hurd/fshelp.h>

/* Include the hook calling macros and non-rpc hook definitions (to get
   those, include "trees-s-hooks.h").  */
#include "treefs-hooks.h"

#ifdef TREEFS_DEFINE_EI
#define TREEFS_EI
#else
#define TREEFS_EI __extern_inline
#endif

/* ---------------------------------------------------------------- */

typedef void (**treefs_hook_vector_t)();

/* A list of nodes.  */
struct treefs_node_list;

/* Each user port referring to a file points to one of these.  */
struct treefs_handle
{
  struct port_info pi;
  struct treefs_auth *auth;	/* User identification */
  struct treefs_peropen *po;	/* The io object itself */
  void *u;			/* for user use */
};

/* An authentication cookie.  */
struct treefs_auth
{
  int refs;
  uid_t *uids, *gids;
  int nuids, ngids;
  int isroot;
  void *u;			/* for user use */
};

/* Bits the user is permitted to set with io_*_openmodes */
#define TREEFS_SETTABLE_FLAGS (O_APPEND|O_ASYNC|O_FSYNC|O_NONBLOCK|O_NOATIME)

/* Bits that are turned off after open */
#define TREEFS_OPENONLY_FLAGS (O_CREAT|O_EXCL|O_NOLINK|O_NOTRANS|O_NONBLOCK)

struct treefs_peropen
{
  int refs;
  int open_flags;
  int user_lock_state;

  /* A port to the directory through which this open file was reached.  */
  mach_port_t parent_port;

  void *u;			/* for user use */

  struct treefs_node *node;
};

/* A filesystem node in the tree.  */
struct treefs_node
{
  io_statbuf_t stat;
  struct treefs_fsys *fsys;

  struct trans_link active_trans;
  char *passive_trans;
  struct lock_box user_lock;

  pthread_mutex_t lock;
  unsigned refs, weak_refs;

  /* Node ops */
  treefs_hook_vector_t hooks;

  /* If this node is a directory, then this is the directory state.  */
  struct treefs_node_list *children;

  void *u;			/* for user use */
};

struct treefs_node_list
{
  struct treefs_node **nodes;
  unsigned short num_nodes, nodes_alloced;
  char *names;
  unsigned short names_len, names_alloced;
};

struct treefs_fsys
{
  struct port_info pi;
  pthread_mutex_t lock;

  /* The root node in this filesystem.  */
  struct treefs_node *root;

  /* The port for the node which this filesystem is translating.  */
  mach_port_t underlying_port;
  /* And stat info for it.  */
  io_statbuf_t underlying_stat;

  /* Flags from the TREEFS_FSYS_ set.  */
  int flags;
  /* Max number of symlink expansions allowed.  */
  unsigned max_symlinks;
  /* Sync interval (in seconds).  0 means all operations should be
     synchronous, any negative value means never sync.  */
  int sync_interval;

  /* Values to return from a statfs. */
  int fs_type;
  int fs_id;

  /* This is the hook vector that each new node in this filesystem starts out
     with.  */
  treefs_hook_vector_t hooks;

  /* The port bucket to which all of our ports belongs.  */
  struct ports_bucket *port_bucket;

  /* Various classes of ports we know about.  */
  struct port_class *handle_port_class;

  void *u;			/* for user use */
};

/* Filesystem flags.  */
#define TREEFS_FSYS_READONLY  	0x1

/* ---------------------------------------------------------------- */
/* In-core directory management routines (`mdir' == `memory dir').  These are
   intended for keeping non-permanent directory state.  If called on a
   non-dir, ENOTDIR is returned.  */

/* Add CHILD to DIR as NAME, replacing any existing entry.  If OLD_CHILD is
   NULL, and NAME already exists in dir, EEXIST is returned, otherwise, any
   previous child is replaced and returned in OLD_CHILD.  DIR should be
   locked.  */
error_t treefs_mdir_add (struct treefs_node *dir, char *name,
			 struct treefs_node *child,
			 struct treefs_node **old_child);

/* Remove any entry in DIR called NAME.  If there is no such entry, ENOENT is
   returned.  If OLD_CHILD is non-NULL, any removed entry is returned in it.
   DIR should be locked.  */
error_t treefs_mdir_remove (struct treefs_node *dir, char *name,
			    struct treefs_node **old_child);

/* Returns in NODE any entry called NAME in DIR, or NULL (and ENOENT) if
   there isn't such.  DIR should be locked.  */
error_t treefs_mdir_get (struct treefs_node *dir, char *name,
			 struct treefs_node **node);

/* Call FUN on each child of DIR; if FUN returns a non-zero value at any
   point, stop iterating and return that value immediately.  */
error_t treefs_mdir_for_each (struct treefs_node *dir,
			     error_t (*fun)(struct treefs_node *node));

/* ---------------------------------------------------------------- */
/* Functions for dealing with node lists.  */

/* Return a new node list, or NULL if a memory allocation error occurs.  */
struct treefs_node_list *treefs_make_node_list ();

/* Add NODE to LIST as NAME, replacing any existing entry.  If OLD_NODE is
   NULL, and an entry NAME already exists, EEXIST is returned, otherwise, any
   previous child is replaced and returned in OLD_NODE.  */
error_t treefs_node_list_add (struct treefs_node_list *list, char *name,
			      struct treefs_node *node,
			      struct treefs_node **old_node);

/* Remove any entry in LIST called NAME.  If there is no such entry, ENOENT is
   returned.  If OLD_NODE is non-NULL, any removed entry is returned in it.  */
error_t treefs_node_list_remove (struct treefs_node_list *list, char *name,
				 struct treefs_node **old_node);

/* Returns in NODE any entry called NAME in LIST, or NULL (and ENOENT) if
   there isn't such.  */
error_t treefs_node_list_get (struct treefs_node_list *list, char *name,
			      struct treefs_node **node);

/* Call FUN on each node in LIST; if FUN returns a non-zero value at any
   point, stop iterating and return that value immediately.  */
error_t treefs_node_list_for_each (struct treefs_node_list *list,
				   error_t (*fun)(char *name,
						  struct treefs_node *node));

/* ---------------------------------------------------------------- */
/* Functions for manipulating hook vectors.  */

typedef void (*treefs_hook_vector_init_t[TREEFS_NUM_HOOKS])();

extern treefs_hook_vector_init_t treefs_default_hooks;

/* Returns a copy of the treefs hook vector HOOKS, or a zero'd vector if HOOKS
   is NULL.  If HOOKS is NULL, treefs_default_hooks is used.  If a memory
   allocation error occurs, NULL is returned.  */
treefs_hook_vector_t treefs_hooks_clone (treefs_hook_vector_t hooks);

/* Copies each non-NULL entry in OVERRIDES into HOOKS.  */
void treefs_hooks_override (treefs_hook_vector_t hooks,
			    treefs_hook_vector_t overrides);

/* Sets the hook NUM in HOOKS to HOOK.  */
void treefs_hooks_set (treefs_hook_vector_t hooks,
		       unsigned num, void (*hook)());

/* ---------------------------------------------------------------- */
/* Reference counting function (largely stolen from diskfs).  */

extern pthread_spinlock_t treefs_node_refcnt_lock;

extern void treefs_node_ref (struct treefs_node *node);
extern void treefs_node_release (struct treefs_node *node);
extern void treefs_node_unref (struct treefs_node *node);
extern void treefs_node_ref_weak (struct treefs_node *node);
extern void treefs_node_release_weak (struct treefs_node *node);
extern void treefs_node_unref_weak (struct treefs_node *node);

#if defined(__USE_EXTERN_INLINES) || defined(TREEFS_DEFINE_EI)
/* Add a hard reference to a node.  If there were no hard
   references previously, then the node cannot be locked
   (because you must hold a hard reference to hold the lock). */
TREEFS_EI void
treefs_node_ref (struct treefs_node *node)
{
  int new_ref;
  pthread_spin_lock (&treefs_node_refcnt_lock);
  node->refs++;
  new_ref = (node->refs == 1);
  pthread_spin_unlock (&treefs_node_refcnt_lock);
  if (new_ref)
    {
      pthread_mutex_lock (&node->lock);
      treefs_node_new_refs (node);
      pthread_mutex_unlock (&node->lock);
    }
}

/* Unlock node NODE and release a hard reference; if this is the last
   hard reference and there are no links to the file then request
   weak references to be dropped.  */
TREEFS_EI void
treefs_node_release (struct treefs_node *node)
{
  int tried_drop_weak_refs = 0;

 loop:
  pthread_spin_lock (&treefs_node_refcnt_lock);
  assert_backtrace (node->refs);
  node->refs--;
  if (node->refs + node->weak_refs == 0)
    treefs_node_drop (node);
  else if (node->refs == 0 && !tried_drop_weak_refs)
    {
      pthread_spin_unlock (&treefs_node_refcnt_lock);
      treefs_node_lost_refs (node);
      if (treefs_node_unlinked (node))
	{
	  /* There are no links.  If there are weak references that
	     can be dropped, we can't let them postpone deallocation.
	     So attempt to drop them.  But that's a user-supplied
	     routine, which might result in further recursive calls to
	     the ref-counting system.  So we have to reacquire our
	     reference around the call to forestall disaster. */
	  pthread_spin_unlock (&treefs_node_refcnt_lock);
	  node->refs++;
	  pthread_spin_unlock (&treefs_node_refcnt_lock);

	  treefs_node_try_dropping_weak_refs (node);

	  /* But there's no value in looping forever in this
	     routine; only try to drop weak references once. */
	  tried_drop_weak_refs = 1;

	  /* Now we can drop the reference back... */
	  goto loop;
	}
    }
  else
    pthread_spin_unlock (&treefs_node_refcnt_lock);
  pthread_mutex_unlock (&node->lock);
}

/* Release a hard reference on NODE.  If NODE is locked by anyone, then
   this cannot be the last hard reference (because you must hold a
   hard reference in order to hold the lock).  If this is the last
   hard reference and there are no links, then request weak references
   to be dropped.  */
TREEFS_EI void
treefs_node_unref (struct treefs_node *node)
{
  int tried_drop_weak_refs = 0;

 loop:
  pthread_spin_lock (&treefs_node_refcnt_lock);
  assert_backtrace (node->refs);
  node->refs--;
  if (node->refs + node->weak_refs == 0)
    {
      pthread_mutex_lock (&node->lock);
      treefs_node_drop (node);
    }
  else if (node->refs == 0)
    {
      pthread_mutex_lock (&node->lock);
      pthread_spin_unlock (&treefs_node_refcnt_lock);
      treefs_node_lost_refs (node);
      if (treefs_node_unlinked(node) && !tried_drop_weak_refs)
	{
	  /* Same issue here as in nodeut; see that for explanation */
	  pthread_spin_unlock (&treefs_node_refcnt_lock);
	  node->refs++;
	  pthread_spin_unlock (&treefs_node_refcnt_lock);

	  treefs_node_try_dropping_weak_refs (node);
	  tried_drop_weak_refs = 1;

	  /* Now we can drop the reference back... */
	  pthread_mutex_unlock (&node->lock);
	  goto loop;
	}
      pthread_mutex_unlock (&node->lock);
    }
  else
    pthread_spin_unlock (&treefs_node_refcnt_lock);
}

/* Add a weak reference to a node. */
TREEFS_EI void
treefs_node_ref_weak (struct treefs_node *node)
{
  pthread_spin_lock (&treefs_node_refcnt_lock);
  node->weak_refs++;
  pthread_spin_unlock (&treefs_node_refcnt_lock);
}

/* Unlock node NODE and release a weak reference */
TREEFS_EI void
treefs_node_release_weak (struct treefs_node *node)
{
  pthread_spin_lock (&treefs_node_refcnt_lock);
  assert_backtrace (node->weak_refs);
  node->weak_refs--;
  if (node->refs + node->weak_refs == 0)
    treefs_node_drop (node);
  else
    {
      pthread_spin_unlock (&treefs_node_refcnt_lock);
      pthread_mutex_unlock (&node->lock);
    }
}

/* Release a weak reference on NODE.  If NODE is locked by anyone, then
   this cannot be the last reference (because you must hold a
   hard reference in order to hold the lock).  */
TREEFS_EI void
treefs_node_unref_weak (struct treefs_node *node)
{
  pthread_spin_lock (&treefs_node_refcnt_lock);
  assert_backtrace (node->weak_refs);
  node->weak_refs--;
  if (node->refs + node->weak_refs == 0)
    {
      pthread_mutex_lock (&node->lock);
      treefs_node_drop (node);
    }
  else
    pthread_spin_unlock (&treefs_node_refcnt_lock);
}
#endif /* Use extern inlines.  */

/* ---------------------------------------------------------------- */

/* Return in PORT a send right for a new handle, pointing at the peropen PO,
   with rights initialized from AUTH.  */
error_t
treefs_peropen_create_right (struct treefs_peropen *po,
			     struct treefs_auth *auth,
			     mach_port_t *port);

/* Return a send right for a new handle and a new peropen, pointing at NODE,
   with rights initialized from AUTH.  MODE and PARENT_PORT are used to
   initialize the corresponding fields in the new peropen.  */
error_t
treefs_node_create_right (struct treefs_node *node, int flags,
			  mach_port_t parent_port, struct treefs_auth *auth,
			  mach_port_t *port);

/* ---------------------------------------------------------------- */
/* Auth functions; copied from diskfs.  */

extern int treefs_auth_has_uid (struct treefs_auth *auth, uid_t uid);
extern int treefs_auth_in_group (struct treefs_auth *auth, gid_t gid);

#if defined(__USE_EXTERN_INLINES) || defined(TREEFS_DEFINE_EI)
/* Return nonzero iff the user identified by AUTH has uid UID. */
TREEFS_EI int
treefs_auth_has_uid (struct treefs_auth *auth, uid_t uid)
{
  int i;
  for (i = 0; i < auth->nuids; i++)
    if (auth->uids[i] == uid)
      return 1;
  return 0;
}

/* Return nonzero iff the user identified by AUTH has group GID. */
TREEFS_EI int
treefs_auth_in_group (struct treefs_auth *auth, gid_t gid)
{
  int i;
  for (i = 0; i < auth->ngids; i++)
    if (auth->gids[i] == gid)
      return 1;
  return 0;
}
#endif /* Use extern inlines.  */

/* ---------------------------------------------------------------- */
/* Helper routines for dealing with translators.  */

/* Return the active translator control port for NODE.  If there is no
   translator, active or passive, MACH_PORT_NULL is returned in CONTROL_PORT.
   If there is a translator, it is started if necessary, and returned in
   CONTROL_PORT.  *DIR_PORT should be a port right to use as the new
   translators parent directory.  If it is MACH_PORT_NULL, a port is created
   from DIR and PARENT_PORT and stored in *DIR_PORT; otherwise DIR and
   PARENT_PORT are not used.  Neither NODE or DIR should be locked when
   calling this function.  */
error_t treefs_node_get_active_trans (struct treefs_node *node,
				      struct treefs_node *dir,
				      mach_port_t parent_port,
				      mach_port_t *control_port,
				      mach_port_t *dir_port);

/* Drop the active translator CONTROL_PORT on NODE, unless it's no longer the
   current active translator, in which case just drop a reference to it.  */
void treefs_node_drop_active_trans (struct treefs_node *node,
				    mach_port_t control_port);

/* ---------------------------------------------------------------- */
/* Basic node creation.  */

/* Create a basic node, with one reference and no user-specific fields
   initialized, and return it in NODE */
error_t
treefs_create_node (struct treefs_fsys *fsys, struct treefs_node **node);

/* Immediately destroy NODE, with no user-finalization.  */
error_t treefs_free_node (struct treefs_node *node);

/* ---------------------------------------------------------------- */
/* Some global variables.  */

/* The port class used by treefs to make filesystem control ports.  */
struct port_class *treefs_fsys_port_class;

#endif /* __TREEFS_H__ */
