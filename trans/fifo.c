/* A translator for fifos

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <unistd.h>
#include <error.h>
#include <string.h>
#include <fcntl.h>

#include <cthreads.h>
#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd/fsys.h>
#include <hurd/pipe.h>

/* Global options.  These defaults are the standard ones, I think...   */
int wait_for_reader = 1;
int one_reader = 1;

/* What kinds of pipes we use.  */
struct pipe_class *fifo_pipe_class;

/* The current fifo that new opens will see, or NULL if there is none.  */
struct pipe *active_fifo = NULL;

/* Lock this when changing ACTIVE_FIFO.  */
struct mutex active_fifo_lock;
/* Signal this when ACTIVE_FIFO may have changed.  */
struct condition active_fifo_changed;

/* ---------------------------------------------------------------- */

#define USAGE "Usage: %s [OPTION...]\n"

static void
usage(int status)
{
  if (status != 0)
    fprintf(stderr, "Try `%s --help' for more information.\n",
	    program_invocation_name);
  else
    {
      printf(USAGE, program_invocation_name);
      printf("\
\n\
  -r, --multiple-readers     Allow multiple simultaneous readers\n\
  -n, --noblock              Don't block on open if there are no readers\n\
  -d, --dgram                Reads reflect write record boundaries\n\
      --help                 Give this usage message\n\
");
    }

  exit(status);
}

#define SHORT_OPTIONS "&"

static struct option options[] =
{
  {"multiple-readers", no_argument, 0, 'r'},
  {"noblock", no_argument, 0, 'n'},
  {"dgram", no_argument, 0, 'd'},
  {"help", no_argument, 0, '&'},
  {0, 0, 0, 0}
};

/* ---------------------------------------------------------------- */

struct port_bucket *port_bucket;

struct port_class *trivfs_protid_portclasses[1];
struct port_class *trivfs_cntl_portclasses[1];
int trivfs_protid_nportclasses = 1;
int trivfs_cntl_nportclasses = 1;

void
main (int argc, char **argv)
{
  int opt;
  error_t err;
  mach_port_t bootstrap;

  fifo_pipe_class = stream_pipe_class;

  while ((opt = getopt_long(argc, argv, SHORT_OPTIONS, options, 0)) != EOF)
    switch (opt)
      {
      case 'r': one_reader = 0; break;
      case 'n': wait_for_reader = 0; break;
      case 'd': fifo_pipe_class = seqpack_pipe_class;
      case '&': usage(0);
      default:  usage(1);
      }
  
  port_bucket = ports_create_bucket ();
  trivfs_protid_portclasses[0] = ports_create_class (trivfs_clean_protid, 0);
  trivfs_cntl_portclasses[0] = ports_create_class (trivfs_clean_cntl, 0);

  if (argc != 1)
    {
      fprintf(stderr, "Usage: %s", program_invocation_name);
      exit(1);
    }

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error(1, 0, "must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup(bootstrap,
		       trivfs_cntl_portclasses[0], port_bucket,
		       trivfs_protid_portclasses[0], port_bucket,
		       NULL);
  if (err)
    error(3, err, "Contacting parent");

  /* Launch. */
  ports_manage_port_operations_multithread (port_bucket,
					    trivfs_demuxer,
					    0, 0, 0, MACH_PORT_NULL);

  exit(0);
}

/* ---------------------------------------------------------------- */

static error_t
open_hook (struct trivfs_peropen *po)
{
  error_t err = 0;
  int flags = po->openmodes;

  mutex_lock (&active_fifo_lock);

  if (flags & O_READ)
    /* When opening for read, what we do depends on what mode this server is
       running in.  The default (if ONE_READER is set) is to only allow one
       reader at a time, with additional opens for read blocking here until
       the old reader goes away; otherwise, we allow multiple readers.  */
    {
      if (one_reader)
	/* Wait until there isn't any active fifo, so we can make one. */
	while (active_fifo != NULL || active_fifo->readers > 0)
	  if (flags & O_NONBLOCK)
	    {
	      err = EWOULDBLOCK;
	      break;
	    }
	  else
	    condition_wait (&active_fifo_changed, &active_fifo_lock);

      if (!err && active_fifo == NULL)
	/* No other readers, and indeed, no fifo; make one.  */
	err = pipe_create (fifo_pipe_class, &active_fifo);
      if (!err)
	{
	  pipe_add_reader (active_fifo);
	  condition_broadcast (&active_fifo_changed);
	  /* We'll unlock ACTIVE_FIFO_LOCK below; the writer code won't make
	     us block because we've ensured that there's a reader for it.  */
	}
    }

  if (!err && (flags & O_WRITE))
    /* Open the active_fifo for writing.  If WAIT_FOR_READER is true, then we
       block until there's someone to read what we wrote, otherwise, if
       there's no fifo, we create one, which we just write into and leave it
       for someone to read later.  */
    {
      if (wait_for_reader)
	/* Wait until there's a fifo to write to.  */
	while (active_fifo == NULL || active_fifo->readers == 0)
	  if (flags & O_NONBLOCK)
	    {
	      err = EWOULDBLOCK;
	      break;
	    }
	  else
	    condition_wait (&active_fifo_changed, &active_fifo_lock);

      if (!err && active_fifo == NULL)
	/* No other readers, and indeed, no fifo; make one.  */
	err = pipe_create (fifo_pipe_class, &active_fifo);
      if (!err)
	pipe_add_writer (active_fifo);
    }

  po->hook = active_fifo;

  mutex_unlock (&active_fifo_lock);

  return err;
}

static void
close_hook (struct trivfs_peropen *po)
{
  int was_active, going_away = 0;
  int flags = po->openmodes;
  struct pipe *pipe = po->hook;

  mutex_lock (&active_fifo_lock);
  was_active = (active_fifo == pipe);

  if (was_active)
    {
      /* We're the last reader; when we're gone there is no more joy.  */
      going_away = ((flags & O_READ) && pipe->readers == 1);
    }
  else
    /* Let others have their fun.  */
    mutex_unlock (&active_fifo_lock);

  if (flags & O_READ)
    pipe_remove_reader (pipe);
  if (flags & O_WRITE)
    pipe_remove_writer (pipe);
  /* At this point, PIPE may be gone, so we can't look at it again.  */

  if (was_active)
    {
      if (going_away)
	active_fifo = NULL;
      condition_broadcast (&active_fifo_changed);
      mutex_unlock (&active_fifo_lock);
    }
}

/* Trivfs hooks  */

int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;

int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ | O_WRITE;

error_t (*trivfs_peropen_create_hook) (struct trivfs_peropen *) = open_hook;
void (*trivfs_peropen_destroy_hook) (struct trivfs_peropen *) = close_hook;

void
trivfs_modify_stat (struct stat *st)
{
  st->st_size = 0;
  st->st_blocks = 0;
  st->st_mode &= ~S_IFMT;
  st->st_mode |= S_IFIFO;
  /* As we try to be clever with large transfers, ask for them. */
  st->st_blksize = vm_page_size * 16;
}

error_t
trivfs_goaway (int flags, mach_port_t realnode, 
	       struct port_class *control_class, 
	       struct port_class *protid_class)
{
  exit(0);
}

/* ---------------------------------------------------------------- */

/* Return objects mapping the data underlying this memory object.  If
   the object can be read then memobjrd will be provided; if the
   object can be written then memobjwr will be provided.  For objects
   where read data and write data are the same, these objects will be
   equal, otherwise they will be disjoint.  Servers are permitted to
   implement io_map but not io_map_cntl.  Some objects do not provide
   mapping; they will set none of the ports and return an error.  Such
   objects can still be accessed by io_read and io_write.  */
error_t
trivfs_S_io_map(struct trivfs_protid *cred,
		memory_object_t *rdobj,
		mach_msg_type_name_t *rdtype,
		memory_object_t *wrobj,
		mach_msg_type_name_t *wrtype)
{
  return EINVAL;
}

/* ---------------------------------------------------------------- */

/* Read data from an IO object.  If offset if -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in AMT.  */
error_t
trivfs_S_io_read (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  char **data, size_t *data_len,
		  off_t offs, size_t amount)
{
  error_t err;

  if (!cred)
    err = EOPNOTSUPP;
  else if (!(cred->po->openmodes & O_READ))
    err = EBADF;
  else
    {
      struct pipe *pipe = cred->po->hook;
      mutex_lock (&pipe->lock);
      err = pipe_read (pipe, cred->po->openmodes & O_NONBLOCK, NULL,
		       data, data_len, amount);
      mutex_unlock (&pipe->lock);
    }

  return err;
}

/* ---------------------------------------------------------------- */

/* Tell how much data can be read from the object without blocking for
   a "long time" (this should be the same meaning of "long time" used
   by the nonblocking flag.  */
error_t
trivfs_S_io_readable (struct trivfs_protid *cred,
		      mach_port_t reply, mach_msg_type_name_t reply_type,
		      size_t *amount)
{
  error_t err;

  if (!cred)
    err = EOPNOTSUPP;
  else if (!(cred->po->openmodes & O_READ))
    err = EBADF;
  else
    {
      struct pipe *pipe = cred->po->hook;
      mutex_lock (&pipe->lock);
      *amount = pipe_readable (pipe, 1);
      mutex_unlock (&pipe->lock);
      err = 0;
    }

  return err;
}

/* ---------------------------------------------------------------- */

/* Change current read/write offset */
error_t
trivfs_S_io_seek (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  off_t offset, int whence, off_t *new_offset)
{
  if (!cred)
    return EOPNOTSUPP;
  return ESPIPE;
}

/* ---------------------------------------------------------------- */

/* SELECT_TYPE is the bitwise OR of SELECT_READ, SELECT_WRITE, and SELECT_URG.
   Block until one of the indicated types of i/o can be done "quickly", and
   return the types that are then available.  ID_TAG is returned as passed; it
   is just for the convenience of the user in matching up reply messages with
   specific requests sent.  */
error_t
trivfs_S_io_select (struct trivfs_protid *cred,
		    mach_port_t reply, mach_msg_type_name_t reply_type,
		    int *type, int *tag)
{
  if (!cred)
    return EOPNOTSUPP;
  else if (((*type & SELECT_READ) && !(cred->po->openmodes & O_READ))
	   || ((*type & SELECT_WRITE) && !(cred->po->openmodes & O_WRITE)))
    return EBADF;
  else
    *type &= ~SELECT_URG;
  return 0;
}

/* ---------------------------------------------------------------- */

/* Write data to an IO object.  If offset is -1, write at the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount successfully written is returned in amount.  A
   given user should not have more than one outstanding io_write on an
   object at a time; servers implement congestion control by delaying
   responses to io_write.  Servers may drop data (returning ENOBUFS)
   if they recevie more than one write when not prepared for it.  */
error_t
trivfs_S_io_write (struct trivfs_protid *cred,
		   mach_port_t reply, mach_msg_type_name_t reply_type,
		   char *data, size_t data_len,
		   off_t offs, size_t *amount)
{
  error_t err;

  if (!cred)
    err = EOPNOTSUPP;
  else if (!(cred->po->openmodes & O_WRITE))
    err = EBADF;
  else
    {
      struct pipe *pipe = cred->po->hook;
      mutex_lock (&pipe->lock);
      err = pipe_write (pipe, NULL, data, data_len, amount);
      mutex_unlock (&pipe->lock);
    }

  return err;
}

/* ---------------------------------------------------------------- */

/* Truncate file.  */
error_t
trivfs_S_file_truncate (struct trivfs_protid *cred, off_t size)
{
  return 0;
}

/* ---------------------------------------------------------------- */
/* These four routines modify the O_APPEND, O_ASYNC, O_FSYNC, and
   O_NONBLOCK bits for the IO object. In addition, io_get_openmodes
   will tell you which of O_READ, O_WRITE, and O_EXEC the object can
   be used for.  The O_ASYNC bit affects icky async I/O; good async 
   I/O is done through io_async which is orthogonal to these calls. */

error_t
trivfs_S_io_get_openmodes (struct trivfs_protid *cred,
			   mach_port_t reply, mach_msg_type_name_t reply_type,
			   int *bits)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    {
      *bits = cred->po->openmodes;
      return 0;
    }
}

error_t
trivfs_S_io_set_all_openmodes(struct trivfs_protid *cred,
			      mach_port_t reply,
			      mach_msg_type_name_t reply_type,
			      int mode)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return 0;
}

error_t
trivfs_S_io_set_some_openmodes (struct trivfs_protid *cred,
				mach_port_t reply,
				mach_msg_type_name_t reply_type,
				int bits)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return 0;
}

error_t
trivfs_S_io_clear_some_openmodes (struct trivfs_protid *cred,
				  mach_port_t reply,
				  mach_msg_type_name_t reply_type,
				  int bits)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return 0;
}

/* ---------------------------------------------------------------- */
/* Get/set the owner of the IO object.  For terminals, this affects
   controlling terminal behavior (see term_become_ctty).  For all
   objects this affects old-style async IO.  Negative values represent
   pgrps.  This has nothing to do with the owner of a file (as
   returned by io_stat, and as used for various permission checks by
   filesystems).  An owner of 0 indicates that there is no owner.  */

error_t
trivfs_S_io_get_owner (struct trivfs_protid *cred,
		       mach_port_t reply,
		       mach_msg_type_name_t reply_type,
		       pid_t *owner)
{
  if (!cred)
    return EOPNOTSUPP;
  *owner = 0;
  return 0;
}

error_t
trivfs_S_io_mod_owner (struct trivfs_protid *cred,
		       mach_port_t reply, mach_msg_type_name_t reply_type,
		       pid_t owner)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return EINVAL;
}
