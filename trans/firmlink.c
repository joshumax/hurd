/* A translator for `firmlinks'

   Copyright (C) 1997,98,99,2001,02 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <argp.h>
#include <argz.h>
#include <error.h>
#include <sys/mman.h>

#include <hurd/trivfs.h>

#include <version.h>

#include "libtrivfs/trivfs_io_S.h"

const char *argp_program_version = STANDARD_HURD_VERSION (firmlink);

static const struct argp_option options[] =
{
  { 0 }
};

static const char args_doc[] = "TARGET";
static const char doc[] = "A translator for firmlinks."
"\vA firmlink is sort of half-way between a symbolic link and a hard link:"
"\n"
"\nLike a symbolic link, it is `by name', and contains no actual reference to"
" the target.  However, the lookup returns a node which will redirect parent"
" lookups so that attempts to find the cwd that go through the link will"
" reflect the link name, not the target name.  The target referenced by the"
" firmlink is looked up in the namespace of the translator, not the client.";

/* Link parameters.  */
static char *target = 0;	/* What we translate too.  */

/* Parse a single option/argument.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  if (key == ARGP_KEY_ARG && state->arg_num == 0)
    target = arg;
  else if (key == ARGP_KEY_ARG || key == ARGP_KEY_NO_ARGS)
    argp_usage (state);
  else
    return ARGP_ERR_UNKNOWN;
  return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;

  /* Parse our options...  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (2, err, "Contacting parent");

  /* Launch. */
  ports_manage_port_operations_multithread (fsys->pi.bucket, trivfs_demuxer,
					    2 * 60 * 1000, 0, 0);

  return 0;
}

/* Return in LINK the node that TARGET_NAME resolves to, with its parent
   replaced by PARENT.  FLAGS are the flags to open TARGET_NAME with.  */
static error_t
firmlink (mach_port_t parent, const char *target_name, int flags,
	  mach_port_t *link)
{
  error_t err;
  file_t authed_link;
  file_t target = file_name_lookup (target_name, flags & ~O_CREAT, 0);

  if (target == MACH_PORT_NULL)
    return errno;

  err = file_reparent (target, parent, &authed_link);
  mach_port_deallocate (mach_task_self (), target);
  mach_port_deallocate (mach_task_self (), parent);

  if (! err)
    {
      err = io_restrict_auth (authed_link, link, 0, 0, 0, 0);
      mach_port_deallocate (mach_task_self (), authed_link);
    }

  return err;
}

/* Trivfs hooks  */

int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;

int trivfs_support_read = 1;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ;

/* Return the root node of our file system:  A firmlink to TARGET, unless
   TARGET doesn't exist, in which case we return a symlink-like node.  */
static error_t
getroot (struct trivfs_control *cntl,
	 mach_port_t reply_port, mach_msg_type_name_t reply_port_type,
	 mach_port_t dotdot,
	 uid_t *uids, u_int nuids, uid_t *gids, u_int ngids,
	 int flags,
	 retry_type *do_retry, char *retry_name,
	 mach_port_t *node, mach_msg_type_name_t *node_type)
{
  error_t err = firmlink (dotdot, target, flags, node);

  if (err == ENOENT)
    /* No target?  Act like a link.  */
    return EAGAIN;

  if (! err)
    {
      *node_type = MACH_MSG_TYPE_MOVE_SEND;
      *do_retry = FS_RETRY_REAUTH;
      retry_name[0] = '\0';
    }

  return err;
}

/* Called by trivfs_S_fsys_getroot before any other processing takes place;
   if the return value is EAGAIN, normal trivfs getroot processing continues,
   otherwise the rpc returns with that return value.  */
error_t (*trivfs_getroot_hook) () = getroot;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_size = strlen (target);
  st->st_blocks = 0;
  st->st_mode &= ~S_IFMT;
  st->st_mode |= S_IFLNK;
}

/* Shutdown the filesystem.  */
error_t
trivfs_goaway (struct trivfs_control *cntl, int flags)
{
  error_t err;
  int force = (flags & FSYS_GOAWAY_FORCE);
  struct port_bucket *bucket = ((struct port_info *)cntl)->bucket;

  err = ports_inhibit_bucket_rpcs (bucket);
  if (err == EINTR || (err && !force))
    return err;

  if (ports_count_class (cntl->protid_class) > 0 && !force)
    /* Still some opens, and we're not being forced to go away, so don't.  */
    {
      ports_enable_class (cntl->protid_class);
      ports_resume_bucket_rpcs (bucket);
      return EBUSY;
    }

  exit (0);
}

/* We store the file offset in po->hook (ick!) */

/* Read data from an IO object.  If offset if -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in AMT.  */
error_t
trivfs_S_io_read (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  data_t *data, mach_msg_type_number_t *data_len,
		  loff_t offs, mach_msg_type_number_t amount)
{
  error_t err = 0;

  if (! cred)
    err = EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_READ))
    err = EBADF;
  else
    {
      size_t max = strlen (target);
      intptr_t start = offs >= 0 ? offs : (intptr_t)cred->po->hook;
      if (start < 0)
	return EINVAL;
      if (start + amount > max)
	amount = max - start;
      if (amount > *data_len)
	*data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      err = (*data == MAP_FAILED) ? errno : 0;
      if (!err && amount > 0)
	{
	  memcpy (*data, target + start, amount);
	  if (offs < 0)
	    cred->po->hook = (void *)(start + amount); /* Update PO offset.  */
	}
      *data_len = amount;
    }

  return err;
}

/* Tell how much data can be read from the object without blocking for
   a "long time" (this should be the same meaning of "long time" used
   by the nonblocking flag.  */
error_t
trivfs_S_io_readable (struct trivfs_protid *cred,
		      mach_port_t reply, mach_msg_type_name_t reply_type,
		      mach_msg_type_number_t *amount)
{
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_READ))
    return EBADF;
  else if ((intptr_t)cred->po->hook < 0)
    return EINVAL;
  else
    *amount = strlen (target) - (intptr_t)cred->po->hook;
  return 0;
}

/* Change current read/write offset */
error_t
trivfs_S_io_seek (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  off_t offset, int whence, off_t *new_offset)
{
  return EOPNOTSUPP;
}

/* SELECT_TYPE is the bitwise OR of SELECT_READ, SELECT_WRITE, and SELECT_URG.
   Block until one of the indicated types of i/o can be done "quickly", and
   return the types that are then available.  ID_TAG is returned as passed; it
   is just for the convenience of the user in matching up reply messages with
   specific requests sent.  */
error_t
trivfs_S_io_select (struct trivfs_protid *cred,
		    mach_port_t reply, mach_msg_type_name_t reply_type,
		    int *type)
{
  return EOPNOTSUPP;
}

error_t
trivfs_S_io_select_timeout (struct trivfs_protid *cred,
			    mach_port_t reply, mach_msg_type_name_t reply_type,
			    struct timespec ts,
			    int *type)
{
  return trivfs_S_io_select (cred, reply, reply_type, type);
}

error_t
trivfs_append_args (struct trivfs_control *fsys,
		    char **argz, size_t *argz_len)
{
  return argz_add (argz, argz_len, "");
}

error_t trivfs_get_source (char *source, size_t source_len)
{
  strncpy (source, target, source_len - 1);
  source[source_len -1 ] = '\0';
  return 0;
}
