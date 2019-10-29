/*
   Copyright (C) 1994-1999, 2002, 2013-2019 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __TRIVFS_H__
#define __TRIVFS_H__

#include <errno.h>
#include <pthread.h>		/* for mutexes &c */
#include <sys/types.h>		/* for uid_t &c */
#include <mach/mach.h>
#include <hurd/ports.h>
#include <hurd/iohelp.h>
#include <hurd/fshelp.h>
#include <refcount.h>

struct trivfs_protid
{
  struct port_info pi;
  struct iouser *user;
  int isroot;			/* Opened by a privileged user, either
				   root or our own user.  */
  /* REALNODE will be null if this protid wasn't fully created (currently
     only in the case where trivfs_protid_create_hook returns an error).  */
  mach_port_t realnode;		/* restricted permissions */
  void *hook;			/* for user use */
  struct trivfs_peropen *po;
};

struct trivfs_peropen
{
  void *hook;			/* for user use */
  int openmodes;
  refcount_t refcnt;
  struct trivfs_control *cntl;

  struct rlock_peropen lock_status;
  struct trivfs_node *tp;
};

/* A unique one of these exists for each node currently in use. */
struct trivfs_node
{
  pthread_mutex_t lock;

  /* The number of references to this node.  */
  int references;

  struct transbox transbox;
  struct rlock_box credlock;
};

struct trivfs_control
{
  struct port_info pi;
  struct port_class *protid_class;
  struct port_bucket *protid_bucket;
  mach_port_t filesys_id;
  mach_port_t file_id;
  mach_port_t underlying;
  void *hook;			/* for user use */
};


/* The user must define these variables. */
extern int trivfs_fstype;
extern int trivfs_fsid;

/* Set these if trivfs should allow read, write,
   or execute of file.    */
extern int trivfs_support_read;
extern int trivfs_support_write;
extern int trivfs_support_exec;

/* Set this some combination of O_READ, O_WRITE, and O_EXEC;
   trivfs will only allow opens of the specified modes.
   (trivfs_support_* is not used to validate opens, only actual
   operations.)  */
extern int trivfs_allow_open;

/* The user must define this function.  This should modify a struct
   stat (as returned from the underlying node) for presentation to
   callers of io_stat.  It is permissible for this function to do
   nothing.  */
void trivfs_modify_stat (struct trivfs_protid *cred, io_statbuf_t *);

/* If this variable is set, it is called to find out what access this
   file permits to USER instead of checking the underlying node.
   REALNODE is the underlying node, and CNTL is the trivfs control
   object.  The access permissions are returned in ALLOWED.  */
error_t (*trivfs_check_access_hook) (struct trivfs_control *cntl,
				     struct iouser *user,
				     mach_port_t realnode,
				     int *allowed);

/* If this variable is set, it is called every time an open happens.
   USER and FLAGS are from the open; CNTL identifies the
   node being opened.  This call need not check permissions on the underlying
   node.  This call can block as necessary, unless O_NONBLOCK is set
   in FLAGS.  Any desired error can be returned, which will be reflected
   to the user and prevent the open from succeeding.  */
error_t (*trivfs_check_open_hook) (struct trivfs_control *cntl,
				   struct iouser *user, int flags);

/* If this variable is set, it is called in place of `trivfs_open' (below).  */
error_t (*trivfs_open_hook) (struct trivfs_control *fsys,
			     struct iouser *user,
			     mach_port_t dotdot,
			     int flags,
			     mach_port_t realnode,
			     struct trivfs_protid **cred);

/* If this variable is set, it is called every time a new protid
   structure is created and initialized. */
error_t (*trivfs_protid_create_hook) (struct trivfs_protid *);

/* If this variable is set, it is called every time a new peropen
   structure is created and initialized. */
error_t (*trivfs_peropen_create_hook) (struct trivfs_peropen *);

/* If this variable is set, it is called every time a protid structure
   is about to be destroyed. */
void (*trivfs_protid_destroy_hook) (struct trivfs_protid *);

/* If this variable is set, it is called every time a peropen structure
   is about to be destroyed. */
void (*trivfs_peropen_destroy_hook) (struct trivfs_peropen *);

/* If this variable is set, it is called by trivfs_S_fsys_getroot before any
   other processing takes place; if the return value is EAGAIN, normal trivfs
   getroot processing continues, otherwise the rpc returns with that return
   value.  */
error_t (*trivfs_getroot_hook) (struct trivfs_control *cntl,
				mach_port_t reply_port,
				mach_msg_type_name_t reply_port_type,
				mach_port_t dotdot,
				uid_t *uids, u_int nuids, uid_t *gids, u_int ngids,
				int flags,
				retry_type *do_retry, char *retry_name,
				mach_port_t *node, mach_msg_type_name_t *node_type);

/* Creates a control port for this filesystem and sends it to BOOTSTRAP with
   fsys_startup.  CONTROL_CLASS & CONTROL_BUCKET are passed to the ports
   library to create the control port, and PROTID_CLASS & PROTID_BUCKET are
   used when creating ports representing opens of this node; any of these may
   be zero, in which case an appropriate port class/bucket is created.  If
   CONTROL isn't NULL, the trivfs control port is return in it.  If any error
   occurs sending fsys_startup, it is returned, otherwise 0.  FLAGS specifies
   how to open the underlying node (O_*).  */
error_t trivfs_startup (mach_port_t bootstrap, int flags,
			struct port_class *control_class,
			struct port_bucket *control_bucket,
			struct port_class *protid_class,
			struct port_bucket *protid_bucket,
			struct trivfs_control **control);

/* Create a new trivfs control port, with underlying node UNDERLYING, and
   return it in CONTROL.  CONTROL_CLASS & CONTROL_BUCKET are passed to
   the ports library to create the control port, and PROTID_CLASS &
   PROTID_BUCKET are used when creating ports representing opens of this
   node; any of these may be zero, in which case an appropriate port
   class/bucket is created.  */
error_t
trivfs_create_control (mach_port_t underlying,
		       struct port_class *control_class,
		       struct port_bucket *control_bucket,
		       struct port_class *protid_class,
		       struct port_bucket *protid_bucket,
		       struct trivfs_control **control);

/* Install these as libports cleanroutines for trivfs_protid_class
   and trivfs_cntl_class respectively. */
void trivfs_clean_protid (void *);
void trivfs_clean_cntl (void *);

/* This demultiplexes messages for trivfs ports. */
int trivfs_demuxer (mach_msg_header_t *, mach_msg_header_t *);

/* FIXME: Add descriptions */
struct trivfs_node *trivfs_make_node (struct trivfs_peropen *po);
struct trivfs_peropen *trivfs_make_peropen (struct trivfs_protid *cred);

/* Return a new protid pointing to a new peropen in CRED, with REALNODE as
   the underlying node reference, with the given identity, and open flags in
   FLAGS.  CNTL is the trivfs control object.  */
error_t trivfs_open (struct trivfs_control *fsys,
		     struct iouser *user,
		     unsigned flags,
		     mach_port_t realnode,
		     struct trivfs_protid **cred);

/* Return a duplicate of CRED in DUP, sharing the same peropen and hook.  A
   non-null hook may be used to detect that this is a duplicate by
   trivfs_peropen_create_hook.  */
error_t trivfs_protid_dup (struct trivfs_protid *cred,
			   struct trivfs_protid **dup);

/* The user must define this function.  Someone wants the filesystem
   CNTL to go away.  FLAGS are from the set FSYS_GOAWAY_*. */
error_t trivfs_goaway (struct trivfs_control *cntl, int flags);

/* Call this to set atime for the node to the current time.  */
error_t trivfs_set_atime (struct trivfs_control *cntl);

/* Call this to set mtime for the node to the current time. */
error_t trivfs_set_mtime (struct trivfs_control *cntl);

/* If this is defined or set to an argp structure, it will be used by the
   default trivfs_set_options to handle runtime options parsing.  Redefining
   this is the normal way to add option parsing to a trivfs program.  */
extern struct argp *trivfs_runtime_argp;

/* Set runtime options for FSYS to ARGZ & ARGZ_LEN.  The default definition
   for this routine simply uses TRIVFS_RUNTIME_ARGP (supply FSYS as the argp
   input field).  */
error_t trivfs_set_options (struct trivfs_control *fsys,
			    char *argz, size_t argz_len);

/* Append to the malloced string *ARGZ of length *ARGZ_LEN a NUL-separated
   list of the arguments to this translator.  The default definition of this
   routine simply calls diskfs_append_std_options.  */
error_t trivfs_append_args (struct trivfs_control *fsys,
			    char **argz, size_t *argz_len);

/* The user may define this function.  The function must set SOURCE to
   the source of the translator. The function may return an EOPNOTSUPP
   to indicate that the concept of a source device is not
   applicable. The default function always returns EOPNOTSUPP. */
error_t trivfs_get_source (char *source, size_t source_len);

/* Add the port class *CLASS to the list of control port classes recognized
   by trivfs; if *CLASS is 0, an attempt is made to allocate a new port
   class, which is stored in *CLASS.  */
error_t trivfs_add_control_port_class (struct port_class **class);

/* Remove the previously added dynamic control port class CLASS, freeing it
   if it was allocated by trivfs_add_control_port_class.  */
void trivfs_remove_control_port_class (struct port_class *class);

/* Add the port class *CLASS to the list of protid port classes recognized by
   trivfs; if *CLASS is 0, an attempt is made to allocate a new port class,
   which is stored in *CLASS.  */
error_t trivfs_add_protid_port_class (struct port_class **class);

/* Remove the previously added dynamic protid port class CLASS, freeing it
   if it was allocated by trivfs_add_protid_port_class.  */
void trivfs_remove_protid_port_class (struct port_class *class);

/* Add the port bucket *BUCKET to the list of dynamically allocated port
   buckets; if *bucket is 0, an attempt is made to allocate a new port
   bucket, which is then stored in *bucket.  */
error_t trivfs_add_port_bucket (struct port_bucket **bucket);

/* Remove the previously added dynamic port bucket BUCKET, freeing it
   if it was allocated by trivfs_add_port_bucket.  */
void trivfs_remove_port_bucket (struct port_bucket *bucket);

/* Type-aliases for mig.  */
typedef struct trivfs_protid *trivfs_protid_t;
typedef struct trivfs_control *trivfs_control_t;


/* The following extracts from io_S.h and fs_S.h catch loff_t erroneously
   written off_t and stat64 erroneously written stat,
   or missing -D_FILE_OFFSET_BITS=64 build flag. */

kern_return_t trivfs_S_io_write (trivfs_protid_t io_object,
				 mach_port_t reply,
				 mach_msg_type_name_t replyPoly,
				 data_t data,
				 mach_msg_type_number_t dataCnt,
				 loff_t offset,
				 vm_size_t *amount);

kern_return_t trivfs_S_io_read (trivfs_protid_t io_object,
				mach_port_t reply,
				mach_msg_type_name_t replyPoly,
				data_t *data,
				mach_msg_type_number_t *dataCnt,
				loff_t offset,
				vm_size_t amount);

kern_return_t trivfs_S_io_seek (trivfs_protid_t io_object,
				mach_port_t reply,
				mach_msg_type_name_t replyPoly,
				loff_t offset,
				int whence,
				loff_t *newp);

kern_return_t trivfs_S_io_stat (trivfs_protid_t stat_object,
				mach_port_t reply,
				mach_msg_type_name_t replyPoly,
				io_statbuf_t *stat_info);

kern_return_t trivfs_S_file_set_size (trivfs_protid_t trunc_file,
				      mach_port_t reply,
				      mach_msg_type_name_t replyPoly,
				      loff_t new_size);

kern_return_t trivfs_S_file_get_storage_info (trivfs_protid_t file,
					      mach_port_t reply,
					      mach_msg_type_name_t replyPoly,
					      portarray_t *ports,
					      mach_msg_type_name_t *portsPoly,
					      mach_msg_type_number_t *portsCnt,
					      intarray_t *ints,
					      mach_msg_type_number_t *intsCnt,
					      off_array_t *offsets,
					      mach_msg_type_number_t *offsetsCnt,
					      data_t *data,
					      mach_msg_type_number_t *dataCnt);

kern_return_t trivfs_S_file_statfs (trivfs_protid_t file,
				    mach_port_t reply,
				    mach_msg_type_name_t replyPoly,
				    fsys_statfsbuf_t *info);

#endif /* __TRIVFS_H__ */
