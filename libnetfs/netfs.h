/* 

   Copyright (C) 1994, 1995, 1996, 1997 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef _HURD_NETFS_H_
#define _HURD_NETFS_H_

#include <hurd/ports.h>
#include <hurd/fshelp.h>
#include <hurd/iohelp.h>
#include <assert.h>

#ifndef NETFS_EI
#define NETFS_EI extern inline
#endif

/* This library supports client-side network file system
   implementations.  It is analogous to the diskfs library provided for 
   disk-based filesystems.  */

struct argp;

struct protid
{
  struct port_info pi;
  
  /* User identification */
  struct iouser *user;
  
  /* Object this refers to */
  struct peropen *po;
  
  /* Shared memory I/O information. */
  memory_object_t shared_object;
  struct shared_io *mapped;
};

/* One of these is created for each open */
struct peropen
{
  off_t filepointer;
  int lock_status;
  int refcnt;
  int openstat;

  struct node *np;

  /* The parent of the translator's root node.  */
  mach_port_t root_parent;

  /* If this node is in a shadow tree, the parent of its root.  */
  mach_port_t shadow_root_parent;
  /* If in a shadow tree, its root node in this translator.  */
  struct node *shadow_root;
};

/* A unique one of these exists for each node currently in use. */
struct node
{
  struct node *next, **prevp;
  
  /* Protocol specific stuff. */
  struct netnode *nn;

  struct stat nn_stat;

  int istranslated;
  
  struct mutex lock;
  
  int references;
  
  mach_port_t sockaddr;
  
  int owner;
  
  struct transbox transbox;

  struct lock_box userlock;

  struct conch conch;

  struct dirmod *dirmod_reqs;
};

/* The user must define this function.  Make sure that NP->nn_stat is
   filled with current information.  CRED identifies the user
   responsible for the operation.  */
error_t netfs_validate_stat (struct node *NP, struct iouser *cred);

/* The user must define this function.  This should attempt a chmod
   call for the user specified by CRED on node NODE, to change the
   owner to UID and the group to GID. */
error_t netfs_attempt_chown (struct iouser *cred, struct node *np,
			     uid_t uid, uid_t gid);

/* The user must define this function.  This should attempt a chauthor
   call for the user specified by CRED on node NODE, to change the
   author to AUTHOR. */
error_t netfs_attempt_chauthor (struct iouser *cred, struct node *np,
				uid_t author);

/* The user must define this function.  This should attempt a chmod
   call for the user specified by CRED on node NODE, to change the
   mode to MODE.  Unlike the normal Unix and Hurd meaning of chmod,
   this function is also used to attempt to change files into other
   types.  If such a transition is attempted which is impossible, then
   return EOPNOTSUPP.
  */
error_t netfs_attempt_chmod (struct iouser *cred, struct node *np,
			     mode_t mode);

/* The user must define this function.  Attempt to turn NODE (user CRED)
   into a symlink with target NAME. */
error_t netfs_attempt_mksymlink (struct iouser *cred, struct node *np,
				 char *name);

/* The user must define this function.  Attempt to turn NODE (user
   CRED) into a device.  TYPE is either S_IFBLK or S_IFCHR. */
error_t netfs_attempt_mkdev (struct iouser *cred, struct node *np,
			     mode_t type, dev_t indexes);

/* The user must define this function.  Attempt to set the passive
   translator record for FILE to ARGZ (of length ARGZLEN) for user
   CRED. */
error_t netfs_set_translator (struct iouser *cred, struct node *np,
			      char *argz, size_t argzlen);

/* The user must define this function.  This should attempt a chflags
   call for the user specified by CRED on node NODE, to change the
   flags to FLAGS. */
error_t netfs_attempt_chflags (struct iouser *cred, struct node *np,
			       int flags);

/* The user must define this function.  This should attempt a utimes
   call for the user specified by CRED on node NODE, to change the
   atime to ATIME and the mtime to MTIME. */
error_t netfs_attempt_utimes (struct iouser *cred, struct node *np,
			      struct timespec *atime, struct timespec *mtime);

/* The user must define this function.  This should attempt to set the
   size of the file NODE (for user CRED) to SIZE bytes long. */
error_t netfs_attempt_set_size (struct iouser *cred, struct node *np,
				off_t size);

/* The user must define this function.  This should attempt to fetch
   filesystem status information for the remote filesystem, for the
   user CRED. */
error_t netfs_attempt_statfs (struct iouser *cred, struct node *np,
			      struct statfs *st);

/* The user must define this function.  This should sync the file NP
   completely to disk, for the user CRED.  If WAIT is set, return
   only after sync is completely finished.  */
error_t netfs_attempt_sync (struct iouser *cred, struct node *np,
			    int wait);

/* The user must define this function.  This should sync the entire
   remote filesystem.  If WAIT is set, return only after
   sync is completely finished.  */
error_t netfs_attempt_syncfs (struct iouser *cred, int wait);

/* The user must define this function.  Lookup NAME in DIR for USER;
   set *NP to the found name upon return.  If the name was not found,
   then return ENOENT.  On any error, clear *NP.  (*NP, if found, should
   be locked, this call should unlock DIR no matter what.) */
error_t netfs_attempt_lookup (struct iouser *user, struct node *dir, 
			      char *name, struct node **np);

/* The user must define this function.  Delete NAME in DIR for USER. */
error_t netfs_attempt_unlink (struct iouser *user, struct node *dir,
			      char *name);

/* Note that in this one call, neither of the specific nodes are locked. */
error_t netfs_attempt_rename (struct iouser *user, struct node *fromdir,
			      char *fromname, struct node *todir, 
			      char *toname, int excl);

/* The user must define this function.  Attempt to create a new
   directory named NAME in DIR for USER with mode MODE.  */
error_t netfs_attempt_mkdir (struct iouser *user, struct node *dir,
			     char *name, mode_t mode);

/* The user must define this function.  Attempt to remove directory
   named NAME in DIR for USER. */
error_t netfs_attempt_rmdir (struct iouser *user, 
			     struct node *dir, char *name);


/* The user must define this function.  Create a link in DIR with name
   NAME to FILE for USER.  Note that neither DIR nor FILE are
   locked.  If EXCL is set, do not delete the target, but return EEXIST
   if NAME is already found in DIR.   */
error_t netfs_attempt_link (struct iouser *user, struct node *dir,
			    struct node *file, char *name, int excl);

/* The user must define this function.  Attempt to create an anonymous
   file related to DIR for USER with MODE.  Set *NP to the returned
   file upon success.  No matter what, unlock DIR. */
error_t netfs_attempt_mkfile (struct iouser *user, struct node *dir,
			      mode_t mode, struct node **np);

/* The user must define this function.  Attempt to create a file named
   NAME in DIR for USER with MODE.  Set *NP to the new node upon
   return.  On any error, clear *NP.  *NP should be locked on success;
   no matter what, unlock DIR before returning.  */
error_t netfs_attempt_create_file (struct iouser *user, struct node *dir,
				   char *name, mode_t mode, struct node **np);

/* The user must define this function.  Read the contents of NP (a symlink),
   for USER, into BUF. */
error_t netfs_attempt_readlink (struct iouser *user, struct node *np,
				char *buf);

/* The user must define this function.  Node NP is being opened by USER,
   with FLAGS.  NEWNODE is nonzero if we just created this node.  Return
   an error if we should not permit the open to complete because of a
   permission restriction. */
error_t netfs_check_open_permissions (struct iouser *user, struct node *np,
				      int flags, int newnode);

/* The user must define this function.  Read from the file NP for user
   CRED starting at OFFSET and continuing for up to *LEN bytes.  Put
   the data at DATA.  Set *LEN to the amount successfully read upon
   return.  */
error_t netfs_attempt_read (struct iouser *cred, struct node *np,
			    off_t offset, size_t *len, void *data);

/* The user must define this function.  Write to the file NP for user
   CRED starting at OFSET and continuing for up to *LEN bytes from
   DATA.  Set *LEN to the amount seccessfully written upon return. */
error_t netfs_attempt_write (struct iouser *cred, struct node *np,
			     off_t offset, size_t *len, void *data);

/* The user must define this function.  Return the valid access
   types (bitwise OR of O_READ, O_WRITE, and O_EXEC) in *TYPES for
   file NP and user CRED.   */
error_t netfs_report_access (struct iouser *cred, struct node *np,
			     int *types);

/* The user must define this function.  Create a new user
   from the specified UID and GID arrays. */
struct iouser *netfs_make_user (uid_t *uids, int nuids,
				       uid_t *gids, int ngids);

/* The user must define this function.  Node NP is all done; free
   all its associated storage. */
void netfs_node_norefs (struct node *np);

error_t netfs_get_dirents (struct iouser *, struct node *, int, int, char **,
			   mach_msg_type_number_t *, vm_size_t, int *);

/* Option parsing */

/* Parse and execute the runtime options in ARGZ & ARGZ_LEN.  EINVAL is
   returned if some option is unrecognized.  The default definition of this
   routine will parse them using NETFS_RUNTIME_ARGP, which see.  */
error_t netfs_set_options (char *argz, size_t argz_len);

/* Append to the malloced string *ARGZ of length *ARGZ_LEN a NUL-separated
   list of the arguments to this translator.  The default definition of this
   routine simply calls netfs_append_std_options.  */
error_t netfs_append_args (char **argz, unsigned *argz_len);

/* If this is defined or set to a pointer to an argp structure, it will be
   used by the default netfs_set_options to handle runtime option parsing.
   The default definition is initialized to a pointer to
   NETFS_STD_RUNTIME_ARGP.  Setting this variable is the usual way to add
   option parsing to a program using libnetfs.  */
extern struct argp *netfs_runtime_argp;

/* An argp for the standard netfs runtime options.  The default definition
   of NETFS_RUNTIME_ARGP points to this, although if the user redefines
   that, he may chain this onto his argp as well.  */
extern const struct argp netfs_std_runtime_argp;

/* An argp structure for the standard netfs command line arguments.  The
   user may call argp_parse on this to parse the command line, chain it onto
   the end of his own argp structure, or ignore it completely.  */
extern const struct argp netfs_std_startup_argp;

/* *Appends* to ARGZ & ARGZ_LEN '\0'-separated options describing the standard
   netfs option state (note that unlike netfs_get_options, ARGZ & ARGZ_LEN
   must already have a sane value).  */
error_t netfs_append_std_options (char **argz, size_t *argz_len);

/* Definitions provided by netfs. */
struct node *netfs_make_node (struct netnode *);

mach_port_t netfs_startup (mach_port_t, int);

extern spin_lock_t netfs_node_refcnt_lock;

extern int netfs_maxsymlinks;

void netfs_init (void);
void netfs_server_loop (void);
struct protid *netfs_make_protid (struct peropen *, struct iouser *);

/* Create and return a new peropen structure on node NP with open
   flags FLAGS.  The initial values for the root_parent, shadow_root, and
   shadow_root_parent fields are copied from CONTEXT if it's non-zero,
   otherwise zerod.  */
struct peropen *netfs_make_peropen (struct node *, int,
				    struct peropen *context);

void netfs_drop_node (struct node *);
void netfs_release_protid (void *);
void netfs_release_peropen (struct peropen *);
int netfs_demuxer (mach_msg_header_t *, mach_msg_header_t *);
error_t netfs_shutdown (int);

extern struct port_class *netfs_protid_class;
extern struct port_class *netfs_control_class;
extern struct port_bucket *netfs_port_bucket;
extern struct node *netfs_root_node;
extern mach_port_t netfs_fsys_identity;
extern auth_t netfs_auth_server_port;

NETFS_EI void
netfs_nref (struct node *np)
{
  spin_lock (&netfs_node_refcnt_lock);
  np->references++;
  spin_unlock (&netfs_node_refcnt_lock);
}
  
NETFS_EI void
netfs_nrele (struct node *np)
{
  spin_lock (&netfs_node_refcnt_lock);
  assert (np->references);
  np->references--;
  if (np->references == 0)
    {
      mutex_lock (&np->lock);
      netfs_drop_node (np);
    }
  else
    spin_unlock (&netfs_node_refcnt_lock);
}

NETFS_EI void
netfs_nput (struct node *np)
{
  spin_lock (&netfs_node_refcnt_lock);
  assert (np->references);
  np->references--;
  if (np->references == 0)
    netfs_drop_node (np);
  else
    {
      spin_unlock (&netfs_node_refcnt_lock);
      mutex_unlock (&np->lock);
    }
}



/* Mig gook. */
typedef struct protid *protid_t;


#endif /* _HURD_NETFS_H_ */
