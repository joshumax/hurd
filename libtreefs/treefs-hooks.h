/* Hooks in libtreefs (also see "treefs-s-hooks.h")

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#ifndef __TREEFS_HOOKS_H__
#define __TREEFS_HOOKS_H__

#include "treefs.h"

/* ---------------------------------------------------------------- */

/* Hook indices */
enum
{
  /* file rpcs */
  TREEFS_HOOK_S_FILE_EXEC, TREEFS_HOOK_S_FILE_CHOWN,
  TREEFS_HOOK_S_FILE_CHAUTHOR, TREEFS_HOOK_S_FILE_CHMOD,
  TREEFS_HOOK_S_FILE_CHFLAGS, TREEFS_HOOK_S_FILE_UTIMENS,
  TREEFS_HOOK_S_FILE_SET_SIZE, TREEFS_HOOK_S_FILE_LOCK,
  TREEFS_HOOK_S_FILE_LOCK_STAT, TREEFS_HOOK_S_FILE_ACCESS,
  TREEFS_HOOK_S_FILE_NOTICE, TREEFS_HOOK_S_FILE_SYNC,
  TREEFS_HOOK_S_FILE_GET_LINK_NODE,

  /* io rpcs */
  TREEFS_HOOK_S_IO_WRITE, TREEFS_HOOK_S_IO_READ, TREEFS_HOOK_S_IO_SEEK,
  TREEFS_HOOK_S_IO_READABLE, TREEFS_HOOK_S_IO_SET_ALL_OPENMODES,
  TREEFS_HOOK_S_IO_GET_OPENMODES, TREEFS_HOOK_S_IO_SET_SOME_OPENMODES,
  TREEFS_HOOK_S_IO_CLEAR_SOME_OPENMODES, TREEFS_HOOK_S_IO_ASYNC,
  TREEFS_HOOK_S_IO_MOD_OWNER, TREEFS_HOOK_S_IO_GET_OWNER,
  TREEFS_HOOK_S_IO_GET_ICKY_ASYNC_ID, TREEFS_HOOK_S_IO_SELECT,
  TREEFS_HOOK_S_IO_STAT, TREEFS_HOOK_S_IO_REAUTHENTICATE,
  TREEFS_HOOK_S_IO_RESTRICT_AUTH, TREEFS_HOOK_S_IO_DUPLICATE,
  TREEFS_HOOK_S_IO_SERVER_VERSION, TREEFS_HOOK_S_IO_MAP,
  TREEFS_HOOK_S_IO_MAP_CNTL, TREEFS_HOOK_S_IO_RELEASE_CONCH,
  TREEFS_HOOK_S_IO_EOFNOTIFY, TREEFS_HOOK_S_IO_PRENOTIFY,
  TREEFS_HOOK_S_IO_POSTNOTIFY, TREEFS_HOOK_S_IO_READNOTIFY,
  TREEFS_HOOK_S_IO_READSLEEP, TREEFS_HOOK_S_IO_SIGIO,

  /* directory rpcs */
  TREEFS_HOOK_S_DIR_LOOKUP, TREEFS_HOOK_S_DIR_READDIR, TREEFS_HOOK_S_DIR_MKDIR,
  TREEFS_HOOK_S_DIR_RMDIR, TREEFS_HOOK_S_DIR_UNLINK, TREEFS_HOOK_S_DIR_LINK,
  TREEFS_HOOK_S_DIR_RENAME, TREEFS_HOOK_S_DIR_MKFILE,
  TREEFS_HOOK_S_DIR_NOTICE_CHANGES,

  /* filesystem rpcs */
  TREEFS_HOOK_S_FSYS_GETROOT, TREEFS_HOOK_S_FSYS_SET_OPTIONS,
  TREEFS_HOOK_S_FSYS_SYNCFS, TREEFS_HOOK_S_FSYS_GETFILE, TREEFS_S_FSYS_GOAWAY,

  /* Non-rpc fsys hooks */
  TREEFS_HOOK_FSYS_CREATE_NODE, TREEFS_HOOK_FSYS_DESTROY_NODE,
  TREEFS_HOOK_FSYS_GET_ROOT,

  /* Node hooks */
  TREEFS_HOOK_NODE_TYPE,
  TREEFS_HOOK_NODE_UNLINKED, TREEFS_HOOK_NODE_MOD_LINK_COUNT,
  TREEFS_HOOK_DIR_LOOKUP, TREEFS_HOOK_DIR_NOENT,
  TREEFS_HOOK_DIR_CREATE_CHILD, TREEFS_HOOK_DIR_LINK, TREEFS_HOOK_DIR_UNLINK,
  TREEFS_HOOK_NODE_OWNED, TREEFS_HOOK_NODE_ACCESS,
  TREEFS_HOOK_NODE_GET_SYMLINK, TREEFS_HOOK_NODE_GET_PASSIVE_TRANS,
  TREEFS_HOOK_NODE_START_TRANSLATOR, TREEFS_HOOK_NODE_GET_TRANS_AUTH,
  TREEFS_HOOK_NODE_DROP, TREEFS_HOOK_NODE_INIT, TREEFS_HOOK_DIR_INIT,
  TREEFS_HOOK_NODE_INIT_PEROPEN, TREEFS_HOOK_NODE_INIT_HANDLE,
  TREEFS_HOOK_NODE_FINALIZE, TREEFS_HOOK_DIR_FINALIZE,
  TREEFS_HOOK_NODE_FINALIZE_PEROPEN, TREEFS_HOOK_NODE_FINALIZE_HANDLE,

  /* Reference counting support */
  TREEFS_HOOK_NODE_NEW_REFS, TREEFS_HOOK_NODE_LOST_REFS,
  TREEFS_HOOK_NODE_TRY_DROPPING_WEAK_REFS,

  TREEFS_NUM_HOOKS
};

/* ---------------------------------------------------------------- */
/* Hook calling/defining macros */

/* Call the hook number HOOK in the hook vector HOOKS, whose function is of
   type TYPE, with the args ARGS (my this is a useful comment).  */
#define TREEFS_CALL_HOOK(hooks, hook, type, args...) \
  ((type *)(hooks)[hook])(args)
#define TREEFS_CALL_HANDLE_HOOK(h, hook, type, args...) \
  ({struct treefs_handle *_tfs_cn_h = (h); \
    TREEFS_CALL_HOOK(_tfs_cn_h->po->node->hooks,hook,type, \
		     _tfs_cn_h , ##args);})
#define TREEFS_CALL_NODE_HOOK(node, hook, type, args...) \
  ({struct treefs_node *_tfs_cn_node = (node); \
    TREEFS_CALL_HOOK(_tfs_cn_node->hooks,hook,type, _tfs_cn_node , ##args);})
#define TREEFS_CALL_FSYS_HOOK(fsys, hook, type, args...) \
  ({struct treefs_fsys *_tfs_cn_fsys = (fsys); \
    TREEFS_CALL_HOOK(_tfs_cn_fsys->hooks,hook,type, _tfs_cn_fsys , ##args);})

/* Shorthand form of TREEFS_CALL_*_HOOK (only used here).  */
#define _TREEFS_CHH(h, hook_id, type_id, args...) \
  TREEFS_CALL_HANDLE_HOOK(h, TREEFS_HOOK_##hook_id, treefs_##type_id##_t , ##args)
#define _TREEFS_CNH(node, hook_id, type_id, args...) \
  TREEFS_CALL_NODE_HOOK(node, TREEFS_HOOK_##hook_id, treefs_##type_id##_t , ##args)
#define _TREEFS_CFH(fsys, hook_id, type_id, args...) \
  TREEFS_CALL_FSYS_HOOK(fsys, TREEFS_HOOK_##hook_id, treefs_##type_id##_t , ##args)

/* Forward declare some structures used before definition.  */
struct treefs_node;
struct treefs_fsys;
struct treefs_auth;
struct treefs_handle;
struct treefs_peropen;

/* Shorthand for declaring the various hook types (each hook has an
   associated type so that a user can type-check his hook routine).  */
#define DNH(name_sym, ret_type, argtypes...) \
  typedef ret_type treefs_##name_sym##_t (struct treefs_node * , ##argtypes);
#define DFH(name_sym, ret_type, argtypes...) \
  typedef ret_type treefs_##name_sym##_t (struct treefs_fsys * , ##argtypes);

/* ---------------------------------------------------------------- */
/* Non RPC hooks */

/* Called to get the root node of the a filesystem, with a reference,
   returning it in ROOT, or return an error if it can't be had.  The default
   hook just returns FSYS->root or an error if it's NULL.  Note that despite
   the similar name, this is very different from fsys_s_getroot!  FSYS must
   not be locked.  */
DFH(fsys_get_root, error_t, struct treefs_node **root)
#define treefs_fsys_get_root(fsys, args...) \
  _TREEFS_CFH(fsys, FSYS_GET_ROOT, fsys_get_root , ##args)

/* Called on a filesystem to create a new node in that filesystem, returning
   it, with one reference, in NODE.  DIR, if non-NULL, is the nominal parent
   directory, and MODE and AUTH are the desired mode and user info
   respectively.  This hook should also call node_init_stat & node_init to
   initialize the various user bits.  */
DFH(fsys_create_node, error_t,
    struct treefs_node *dir, mode_t mode, struct treefs_auth *auth,
    struct treefs_node **node)
#define treefs_fsys_create_node(fsys, args...) \
  _TREEFS_CFH(fsys, FSYS_CREATE_NODE, fsys_create_node , ##args)

/* Called on a filesystem to destroy a node in that filesystem.  This call
   should *really* destroy it -- i.e., it's only called once all references
   are gone.  */
DFH(fsys_destroy_node, void, struct treefs_node *node)
#define treefs_fsys_destroy_node(fsys, node) \
  _TREEFS_CFH(fsys, FSYS_DESTROY_NODE, fsys_destroy_node , ##args)

/* Returns the type of NODE, as an S_IFMT value (e.g., S_IFDIR).  The
   default routine just looks at NODE's stat mode.  */
DNH(node_type, int);
#define treefs_node_type(node, args...)					      \
  _TREEFS_CNH(node, NODE_TYPE, node_type , ##args)

#define treefs_node_isdir(node) (treefs_node_type(node) == S_IFDIR)
#define treefs_node_isreg(node) (treefs_node_type(node) == S_IFREG)

/* Return TRUE if NODE is `unlinked' -- that is, can be deleted when all
   (in-memory) references go away.  */
DNH(node_unlinked, int);
#define treefs_node_unlinked(node, args...)				      \
  _TREEFS_CNH(node, NODE_UNLINKED, node_unlinked , ##args)

/* Changes the link count of NODE by CHANGE; if any error is returned, the
   operation trying to change the link count will fail, so filesystems that
   don't support real links can restrict it to 1 or 0.  This is mostly used
   by the in-core directory code when it makes a link.  The default hook uses
   the link field of NODE's stat entry.  */
DNH(node_mod_link_count, error_t, int change);
#define treefs_node_mod_link_count(node, args...) \
  _TREEFS_CNH(node, NODE_MOD_LINK_COUNT, node_mod_link_count , ##args)

/* Lookup NAME in NODE, returning the result in CHILD; AUTH should be used to
   do authentication.  If FLAGS contains O_CREAT, and NAME is not found, then
   an entry should be created with a mode of CREATE_MODE (which includes the
   S_IFMT bits, e.g., S_IFREG means a normal file), unless O_EXCL is also
   set, in which case EEXIST should be returned.  Possible special errors
   returned include: EAGAIN -- result would be the parent of our filesystem
   root.  Note that is a single-level lookup, unlike treefs_s_dir_lookup.  */
DNH(dir_lookup, error_t,
    char *name, struct treefs_auth *auth, int flags, int create_mode,
    struct treefs_node **child)
#define treefs_dir_lookup(dir, args...)				      \
  _TREEFS_CNH(dir, DIR_LOOKUP, dir_lookup , ##args)

/* Called by the default implementation of treefs_dir_lookup (and possibly
   user-versions as well) when a directory lookup returns ENOENT, before a
   new node is created.  This hook may return the desire node in CHILD and
   return 0, or return an error code.  Note that a returned node need not
   actually be in the directory DIR, and indeed may be anonymous.  */
DNH(dir_noent, error_t,
    char *name, struct treefs_auth *auth, int flags, int create_mode,
    struct treefs_node **child);
#define treefs_dir_noent(dir, args...)				      \
  _TREEFS_CNH(dir, DIR_NOENT, dir_noent , ##args)

/* Return in CHILD a new node with one reference, presumably a possible child
   of DIR, with a mode MODE.  All attempts to create a new node go through
   this hook, so it may be overridden to easily control creation (e.g.,
   replacing it with a hook that always returns EPERM).  Note that this
   routine doesn't actually enter the child into the directory, or give the
   node a non-zero link count, that should be done by the caller.  */
DNH(dir_create_child, error_t,
    mode_t mode, struct treefs_auth *auth, struct treefs_node **child);
#define treefs_dir_create_child(dir, args...)				      \
  _TREEFS_CNH(dir, DIR_CREATE_CHILD, dir_create_child , ##args)

/* Link the node CHILD into DIR as NAME, using AUTH to check authentication.
   DIR should be locked and CHILD shouldn't be.  The default hook puts it
   into DIR's in-core directory, and uses a reference to CHILD (this way, a
   node can be linked to both in-core and out-of-core directories and the
   permanent link-count will be right).  */
DNH(dir_link, error_t,
    char *name, struct treefs_node *child, struct treefs_auth *auth)
#define treefs_dir_link(dir, args...) \
  _TREEFS_CNH(dir, DIR_LINK, dir_link , ##args)

/* Remove the entry NAME from DIR, using AUTH to check authentication.  DIR
   should be locked.  The default hook removes NAME from DIR's in-core
   directory.  */
DNH(dir_unlink, error_t, char *name, struct treefs_auth *auth)
#define treefs_dir_unlink(dir, args...) \
  _TREEFS_CNH(dir, DIR_UNLINK, dir_unlink , ##args)

/* Check to see if the user identified by AUTH is permitted to do owner-only
   operations on node NP; if so, return 0; if not, return EPERM. */
DNH(node_owned, error_t, struct treefs_auth *auth)
#define treefs_node_owned(node, args...) \
  _TREEFS_CNH(node, NODE_OWNED, node_owned , ##args)

/* Check to see is the user identified by AUTH is permitted to do 
   operation OP on node NP.  Op is one of S_IREAD, S_IWRITE, or S_IEXEC.
   Return 0 if the operation is permitted and EACCES if not. */
DNH(node_access, error_t, int opt, struct treefs_auth *auth)
#define treefs_node_access(node, args...) \
  _TREEFS_CNH(node, NODE_ACCESS, node_access , ##args)

/* NODE now has no more references; clean all state.  The
   _treefs_node_refcnt_lock must be held, and will be released upon return.
   NODE must be locked.  */
DNH(node_drop, error_t);
#define treefs_node_drop(node, args...)					      \
  _TREEFS_CNH(node, NODE_DROP, node_drop , ##args)

/* Called when a new directory is created, after trees_node_init.  If this
   routine returns an error, the new node will be destroyed and the create
   will fail.  */
DNH(dir_init, error_t)
#define treefs_dir_init(dir, args...)					      \
  _TREEFS_CNH(dir, DIR_INIT, dir_init , ##args)

/* If NODE is a symlink, copies the contents into BUF, which should have at
   least *LEN bytes available, and returns 0; if the symlink is too big,
   E2BIG is returned.  Either way, the actual length of the symlink is
   returned in *LEN (so if it's too big, you can allocate an appropriately
   sized buffer and try again).  If NODE is not a symlink, EINVAL is
   returned.  */
DNH(node_get_symlink, error_t, char *buf, int *len)
#define treefs_node_get_symlink(node, args...)				      \
  _TREEFS_CNH(node, NODE_GET_SYMLINK, node_get_symlink , ##args)

/* If NODE has a passive translator, copies the contents into BUF, which
   should have at least *LEN bytes available, and returns 0; if the string is
   too big, E2BIG is returned.  Either way, the actual length of the
   translator string is returned in *LEN (so if it's too big, you can
   allocate an appropriately sized buffer and try again).  If NODE has no
   passive translator, EINVAL is returned.  */
DNH(node_get_passive_trans, error_t, char *buf, int *len)
#define treefs_node_get_passive_trans(node, args...)			      \
  _TREEFS_CNH(node, NODE_GET_PASSIVE_TRANS, node_get_passive_trans , ##args)

/* Returns the user and group that a newly started translator should be
   authenticated as.  The default just returns the owner/group of NODE.  */
DNH(node_get_trans_auth, error_t, uid_t *uid, gid_t *gid)
#define treefs_node_get_trans_auth(node, args...)			      \
  _TREEFS_CNH(node, NODE_GET_TRANS_AUTH, node_get_trans_auth , ##args)

/* Start the translator TRANS (of length TRANS_LEN) on NODE, which should be
   locked, and will be unlocked during the execution of this function.
   PARENT_PORT should be a send right to use as the parent port passed to the
   translator.  */
DNH(node_start_translator, error_t,
    char *trans, unsigned trans_len, file_t parent_port)
#define treefs_node_start_translator(node, args...)			      \
  _TREEFS_CNH(node, NODE_START_TRANSLATOR, node_start_translator , ##args)

/* Called to initialize a new node's stat entry, after all default fields are
   filled in (but before node_init is called).  */ 
DNH(node_init_stat, error_t, 
    struct treefs_node *dir, mode_t mode, struct treefs_auth *auth)
#define treefs_node_init_stat(node, args...) \
  _TREEFS_CNH(node, NODE_INIT_STAT, node_init_stat , ##args)

/* Called to initialize a new node, after all default fields are filled in.
   If this routine returns an error, the new node will be destroyed and the
   create will fail.  */
DNH(node_init, error_t, 
    struct treefs_node *dir, mode_t mode, struct treefs_auth *auth)
#define treefs_node_init(node, args...) \
  _TREEFS_CNH(node, NODE_INIT, node_init , ##args)

/* Called to cleanup node-specific info in a node about to be destroyed.  */
DNH(node_finalize, void)
#define treefs_node_finalize(node, args...) \
  _TREEFS_CNH(node, NODE_FINALIZE, node_finalize , ##args)

/* Called to cleanup node-specific directory info in a node about to be
   destroyed.  Called before node_finalize.  */
DNH(dir_finalize, void)
#define treefs_dir_finalize(dir, args...) \
  _TREEFS_CNH(dir, DIR_FINALIZE, dir_finalize , ##args)

/* Called when the new peropen structure PO is made for NODE, with the
   authorization in AUTH, opened with the flags FLAGS (note that this a copy
   of PO->flags, which the hook may modify).  If an error is returned, the
   open will fail with that error.  The default hook does explicit checks
   against AUTH using treefs_node_access, and otherwise does nothing.  */
DNH(node_init_peropen, error_t,
    struct treefs_peropen *po, int flags, struct treefs_auth *auth)
#define treefs_node_init_peropen(node, args...) 			      \
  _TREEFS_CNH(node, NODE_INIT_PEROPEN, node_init_peropen , ##args)

/* Called the peropen structure PO for NODE is being destroyed.  */
DNH(node_finalize_peropen, void, struct treefs_peropen *po)
#define treefs_node_finalize_peropen(node, args...) 			      \
  _TREEFS_CNH(node, NODE_FINALIZE_PEROPEN, node_finalize_peropen , ##args)

/* Called when a new handle structure is made for a node.  The default does
   nothing.  */
DNH(node_init_handle, void, struct treefs_handle *handle)
#define treefs_node_init_handle(node, args...)				      \
  _TREEFS_CNH(node, NODE_INIT_HANDLE, node_init_handle , ##args)

/* Called when the handle HANDLE for NODE is being destroyed.  */
DNH(node_finalize_handle, void, struct treefs_handle *handle)
#define treefs_node_finalize_handle(node, args...) 			      \
  _TREEFS_CNH(node, NODE_FINALIZE_HANDLE, node_finalize_handle , ##args)

/* ---------------------------------------------------------------- */
/* Ref counting stuff */

/* NODE has just acquired a hard reference where it had none previously.  It
   is thus now OK again to have weak references without real users.  NODE is
   locked. */
DNH(node_new_refs, void);
#define treefs_node_new_refs(node, args...)				      \
  _TREEFS_CNH(node, NODE_NEW_REFS, node_new_refs , ##args)

/* NODE has some weak references but has just lost its last hard reference.
   NP is locked.  */
DNH(node_lost_refs, void);
#define treefs_node_lost_refs(node, args...)				      \
  _TREEFS_CNH(node, NODE_LOST_REFS, node_lost_refs , ##args)

/* NODE has some weak references, but has just lost its last hard references.
   Take steps so that if any weak references can be freed, they are.  NP is
   locked as is the pager refcount lock.  This function will be called after
   treefs_node_lost_refs.  */
DNH(node_try_dropping_weak_refs, void);
#define treefs_node_try_dropping_weak_refs(node, args...)		      \
  _TREEFS_CNH(node, NODE_TRY_DROPPING_WEAK_REFS, node_try_dropping_weak_refs , ##args)

/* Turn off our shorthand notation.  */
#undef DNH
#undef DFH

/* ---------------------------------------------------------------- */
/* Default routines for some hooks (each is the default value for the hook
   with the same name minus the leading underscore).  When you add something
   here, you should also add it to the initialize code in defhooks.c.  */

treefs_fsys_create_node_t _treefs_fsys_create_node;
treefs_fsys_destroy_node_t _treefs_fsys_destroy_node;
treefs_fsys_get_root_t _treefs_fsys_get_root;
treefs_node_type_t _treefs_node_type;
treefs_node_unlinked_t _treefs_node_unlinked;
treefs_node_mod_link_count_t _treefs_node_mod_link_count;
treefs_node_mod_link_count_t _treefs_mod_link_count;
treefs_dir_lookup_t _treefs_dir_lookup;
treefs_dir_noent_t _treefs_dir_noent;
treefs_dir_create_child_t _treefs_dir_create_child;
treefs_dir_link_t _treefs_dir_link;
treefs_dir_unlink_t _treefs_dir_unlink;
treefs_node_owned_t _treefs_node_owned;
treefs_node_access_t _treefs_node_access;
treefs_node_start_translator_t _treefs_node_start_translator;
treefs_node_get_trans_auth_t _treefs_node_get_trans_auth;

#endif /* __TREEFS_HOOKS_H__ */
