/* A translator for fifos

   Copyright (C) 1995,96,97,98,2000,02 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <errno.h>
#include <argp.h>
#include <unistd.h>
#include <error.h>
#include <string.h>
#include <fcntl.h>
#include <assert-backtrace.h>

#include <pthread.h>
#include <hurd.h>
#include <argz.h>
#include <hurd/fshelp.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd/fsys.h>
#include <hurd/pipe.h>
#include <hurd/paths.h>

#include <version.h>

#include "libtrivfs/trivfs_fs_S.h"
#include "libtrivfs/trivfs_fsys_S.h"
#include "libtrivfs/trivfs_io_S.h"

#define DEFAULT_SERVER _SERVERS "fifo";

const char *argp_program_version = STANDARD_HURD_VERSION (new-fifo);

struct port_bucket *port_bucket;
struct port_class *fifo_port_class, *server_port_class, *fsys_port_class;

/* ---------------------------------------------------------------- */

static const struct argp_option options[] =
{
  {"multiple-readers", 'r', 0,     0, "Allow multiple simultaneous readers"},
  {"noblock",          'n', 0,     0, "Don't block on open"},
  {"dgram",            'd', 0,     0, "Reflect write record boundaries"},
  {"server",	       's', 0,     0, "Operate in server mode"},
  {"standalone",       'S', 0,     0, "Don't attempt to use a fifo server"},
  {"use-server",       'U', "NAME",0, "Attempt use server NAME"},
  {0,0}
};

/* Per translator variables.  */
struct fifo_trans
{
  /* True if this not a real translator, but instead a translator server,
     which responds to requests to create translators.  */
  int server;

  /* True if opens for writing should hang until there's a reader.  */
  int wait_for_reader;
  /* True if opens for reading should hang until there's a writer.  */
  int wait_for_writer;
  /* True if opens for read should hang until there are no other readers.  */
  int one_reader;

  /* If non-null, the name of a fifo server to do the translation in our
     stead.  */
  char *use_server;

  /* The translator from which this was initialized.  */
  struct fifo_trans *parent;

  /* What kinds of pipes we use.  */
  struct pipe_class *fifo_pipe_class;

  /* The current fifo that new opens will see, or NULL if there is none.  */
  struct pipe *active_fifo;
  /* Lock this when changing ACTIVE_FIFO.  */
  pthread_mutex_t active_fifo_lock;
  /* Signal this when ACTIVE_FIFO may have changed.  */
  pthread_cond_t active_fifo_changed;
};

/* Return a new FIFO_TRANS in TRANS, initializing it from FROM if it's
   non-null, where possible.  */
static void
fifo_trans_create (struct fifo_trans *from, struct fifo_trans **trans)
{
  struct fifo_trans *new = malloc (sizeof (struct fifo_trans));

  new->server = 0;
  pthread_mutex_init (&new->active_fifo_lock, NULL);
  pthread_cond_init (&new->active_fifo_changed, NULL);

  new->parent = from;

  if (from)
    /* Inherit things that can be inherited.  */
    {
      new->wait_for_reader = from->wait_for_reader;
      new->wait_for_writer = from->wait_for_writer;
      new->one_reader = from->one_reader;
      new->use_server = from->use_server;
      new->fifo_pipe_class = from->fifo_pipe_class;
    }
  else
    /* Otherwise just use default values.  */
    {
      new->wait_for_reader = 1;
      new->wait_for_writer = 1;
      new->one_reader = 1;
      new->use_server = DEFAULT_SERVER;
      new->fifo_pipe_class = stream_pipe_class;
    }

  *trans = new;
}

static void
fifo_trans_free (struct fifo_trans *trans)
{
  free (trans);
}

static error_t
fifo_trans_start (struct fifo_trans *trans, mach_port_t requestor)
{
  struct trivfs_control *control;
  struct port_class *class =
    (trans->server ? server_port_class : fifo_port_class);
  error_t
    err = trivfs_startup (requestor, 0,
			  fsys_port_class, port_bucket, class, port_bucket,
			  &control);
  if (!err)
    control->hook = trans;
  return err;
}

/* Parse our options.  SERVER is true if we are a fifo server providing
   service for others (in which case we don't print error messages).  The
   results of the parse are put into TRANS.  */
static error_t
fifo_trans_parse_args (struct fifo_trans *trans, int argc, char **argv,
		       int print_errs)
{
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'r': trans->one_reader = 0; break;
	case 'n': trans->wait_for_reader = trans->wait_for_writer = 0; break;
	case 'd': trans->fifo_pipe_class = seqpack_pipe_class;
	case 's': trans->server = 1; break;
	case 'U': trans->use_server = arg; break;
	case 'S': trans->use_server = 0; break;
	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, 0, "A translator for fifos." };
  return argp_parse (&argp, argc, argv, print_errs ? 0 : ARGP_SILENT, 0, 0);
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct fifo_trans *trans;
  /* Clean up a fsys control node.  */
  void clean_fsys (void *vfsys)
    {
      struct trivfs_control *fsys = vfsys;
      if (fsys->hook)
	fifo_trans_free (fsys->hook);
      trivfs_clean_cntl (fsys);
    }

  fifo_trans_create (0, &trans);

  if (fifo_trans_parse_args (trans, argc, argv, 1) != 0)
    exit (1);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error(1, 0, "must be started as a translator");

  if (!trans->server && trans->use_server)
    /* Attempt to contact a fifo server to do our work for us.  */
    {
      err = fshelp_delegate_translation (trans->use_server, bootstrap, argv);
      if (!err)
	exit (0);
    }

  err = trivfs_add_port_bucket (&port_bucket);
  if (err)
    error (1, 0, "error creating port bucket");

  err = trivfs_add_control_port_class (&fsys_port_class);
  if (err)
    error (1, 0, "error creating control port class");

  err = trivfs_add_protid_port_class (&fifo_port_class);
  if (err)
    error (1, 0, "error creating protid port class");

  err = trivfs_add_protid_port_class (&server_port_class);
  if (err)
    error (1, 0, "error creating protid port class");

  /* Reply to our parent */
  fifo_trans_start (trans, bootstrap);

  /* Launch. */
  do
    {
      ports_enable_class (fifo_port_class);
      ports_manage_port_operations_multithread (port_bucket,
						trivfs_demuxer,
						30*1000, 5*60*1000, 0);
    }
  while (ports_count_class (fifo_port_class) > 0);

  return 0;
}

/* ---------------------------------------------------------------- */

static error_t
fifo_trans_open (struct fifo_trans *trans, int flags, void **hook)
{
  error_t err = 0;

  if (flags & (O_READ | O_WRITE))
    {
      pthread_mutex_lock (&trans->active_fifo_lock);

/* Wait until the active fifo has changed so that CONDITION is true.  */
#define WAIT(condition, noblock_err)					      \
  while (!err && !(condition))						      \
    {									      \
      if (flags & O_NONBLOCK)						      \
	{								      \
	  err = noblock_err;						      \
	  break;							      \
	}								      \
      else if (pthread_hurd_cond_wait_np (&trans->active_fifo_changed,	      \
					  &trans->active_fifo_lock))	      \
	err = EINTR;							      \
    }

      if (flags & O_READ)
	/* When opening for read, what we do depends on what mode this server
	   is running in.  The default (if ONE_READER is set) is to only
	   allow one reader at a time, with additional opens for read
	   blocking here until the old reader goes away; otherwise, we allow
	   multiple readers.  If WAIT_FOR_WRITER is true, then once we've
	   created a fifo, we also block until someone opens it for writing;
	   otherwise, the first read will block until someone writes
	   something.  */
	{
	  if (trans->one_reader)
	    /* Wait until there isn't any active fifo, so we can make one. */
	    WAIT (!trans->active_fifo || !trans->active_fifo->readers,
		  EWOULDBLOCK);
	  if (!err && trans->active_fifo == NULL)
	    /* No other readers, and indeed, no fifo; make one.  */
	    err = pipe_create (trans->fifo_pipe_class, &trans->active_fifo);
	  if (!err)
	    {
	      pipe_add_reader (trans->active_fifo);
	      pthread_cond_broadcast (&trans->active_fifo_changed);
	      /* We'll unlock ACTIVE_FIFO_LOCK below; the writer code won't
		 make us block because we've ensured that there's a reader
		 for it.  */

	      if (trans->wait_for_writer)
		/* Wait until there's a writer.  */
		{
		  WAIT (trans->active_fifo->writers, 0);
		  if (err)
		    /* Back out the new pipe creation.  */
		    {
		      pipe_remove_reader (trans->active_fifo);
		      trans->active_fifo = NULL;
		      pthread_cond_broadcast (&trans->active_fifo_changed);
		    }
		}
	      else
		/* Otherwise prevent an immediate eof.  */
		trans->active_fifo->flags &= ~PIPE_BROKEN;
	    }
	}

      if (!err && (flags & O_WRITE))
	/* Open the trans->active_fifo for writing.  If WAIT_FOR_READER is
	   true, then we block until there's someone to read what we wrote,
	   otherwise, if there's no fifo, we create one, which we just write
	   into and leave it for someone to read later.  */
	{
	  if (trans->wait_for_reader)
	    /* Wait until there's a fifo to write to.  */
	    WAIT (trans->active_fifo && trans->active_fifo->readers, 0);
	  if (!err && trans->active_fifo == NULL)
	    /* No other readers, and indeed, no fifo; make one.  */
	    {
	      err = pipe_create (trans->fifo_pipe_class, &trans->active_fifo);
	      if (!err)
		trans->active_fifo->flags &= ~PIPE_BROKEN;
	    }
	  if (!err)
	    {
	      pipe_add_writer (trans->active_fifo);
	      pthread_cond_broadcast (&trans->active_fifo_changed);
	    }
	}

      *hook = trans->active_fifo;
    }

  pthread_mutex_unlock (&trans->active_fifo_lock);

  return err;
}

static void
fifo_trans_close (struct fifo_trans *trans, struct trivfs_peropen *po)
{
  int was_active, going_away = 0;
  int flags = po->openmodes;
  struct pipe *pipe = po->hook;

  if (!pipe)
    return;

  pthread_mutex_lock (&trans->active_fifo_lock);
  was_active = (trans->active_fifo == pipe);

  if (was_active)
    /* We're the last reader; when we're gone there is no more joy.  */
    going_away = ((flags & O_READ) && pipe->readers == 1);
  else
    /* Let others have their fun.  */
    pthread_mutex_unlock (&trans->active_fifo_lock);

  if (flags & O_READ)
    pipe_remove_reader (pipe);
  if (flags & O_WRITE)
    pipe_remove_writer (pipe);
  /* At this point, PIPE may be gone, so we can't look at it again.  */

  if (was_active)
    {
      if (going_away)
	trans->active_fifo = NULL;
      pthread_cond_broadcast (&trans->active_fifo_changed);
      pthread_mutex_unlock (&trans->active_fifo_lock);
    }
}

static error_t
open_hook (struct trivfs_peropen *po)
{
  struct fifo_trans *trans = po->cntl->hook;

  if (! trans->server)
    /* We're opening a normal fifo node.  */
    return fifo_trans_open (trans, po->openmodes, &po->hook);
  else if (po->openmodes & (O_READ|O_WRITE|O_APPEND))
    return EPERM;
  else
    /* We're a fifo server serving a new fifo.  */
    return 0;
}

static void
close_hook (struct trivfs_peropen *po)
{
  struct fifo_trans *trans = po->cntl->hook;

  if (! trans->server)
    /* We're closing a normal fifo node.  */
    fifo_trans_close (trans, po);
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
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  struct fifo_trans *trans = cred->po->cntl->hook;
  if (! trans->server)
    /* A fifo node */
    {
      struct pipe *pipe = cred->po->hook;

      st->st_mode &= ~S_IFMT;
      st->st_mode |= S_IFIFO;

      if (pipe)
	{
	  pthread_mutex_lock (&pipe->lock);
	  st->st_size = pipe_readable (pipe, 1);
	  st->st_blocks = st->st_size >> 9;
	  pthread_mutex_unlock (&pipe->lock);
	}
      else
	st->st_size = st->st_blocks = 0;

      /* As we try to be clever with large transfers, ask for them. */
      st->st_blksize = vm_page_size * 16;
    }
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  error_t err;
  int num_opens;
  int force = flags & FSYS_GOAWAY_FORCE;
  int unlink = flags & FSYS_GOAWAY_UNLINK;
  struct fifo_trans *trans = fsys->hook;

  err = ports_inhibit_port_rpcs (fsys);
  if (err == EINTR || (err && !force))
    return err;

  num_opens = ports_count_class (fsys->protid_class);
  if (num_opens > 0 && !force && !unlink)
    /* Still some opens, and we're not being forced to go away, so don't.*/
    {
      ports_enable_class (fsys->protid_class);
      ports_resume_port_rpcs (fsys);
      return EBUSY;
    }

  /* Kill the control connection.  */
  mach_port_deallocate (mach_task_self (), fsys->underlying);
  fsys->underlying = MACH_PORT_NULL;
  ports_destroy_right (fsys);

  if (force)
    /* Kill opens.  */
    {
      error_t maybe_trash_protid (void *vcred)
	{
	  struct trivfs_protid *cred = vcred;
	  if (cred->po->cntl == fsys)
	    {
	      ports_destroy_right (cred);
	      ports_interrupt_rpcs (cred);
	    }
	  return 0;
	}
      ports_bucket_iterate (((struct port_info *)fsys)->bucket,
			    maybe_trash_protid);
    }

  if (! trans->parent)
    /* The root translator, go away, bye bye.  */
    exit (0);

  /* Let things continue; what should die will.  */
  ports_enable_class (fsys->protid_class);
  ports_resume_port_rpcs (fsys);

  return 0;
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
trivfs_S_io_map (struct trivfs_protid *cred,
		 mach_port_t reply, mach_msg_type_name_t replytype,
		 memory_object_t *rdobj,
		 mach_msg_type_name_t *rdtype,
		 memory_object_t *wrobj,
		 mach_msg_type_name_t *wrtype)
{
  return EOPNOTSUPP;
}

/* ---------------------------------------------------------------- */

/* Read data from an IO object.  If offset if -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in AMT.  */
error_t
trivfs_S_io_read (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  data_t *data, size_t *data_len,
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
      assert_backtrace (pipe);
      pthread_mutex_lock (&pipe->lock);
      err = pipe_read (pipe, cred->po->openmodes & O_NONBLOCK, NULL,
		       data, data_len, amount);
      pthread_mutex_unlock (&pipe->lock);
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
      assert_backtrace (pipe);
      pthread_mutex_lock (&pipe->lock);
      *amount = pipe_readable (pipe, 1);
      pthread_mutex_unlock (&pipe->lock);
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
static error_t
io_select_common (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  struct timespec *tsp, int *select_type)
{
  struct pipe *pipe;
  error_t err = 0;
  int ready = 0;

  if (!cred)
    return EOPNOTSUPP;

  pipe = cred->po->hook;

  if (*select_type & SELECT_READ)
    {
      if (cred->po->openmodes & O_READ)
	{
	  pthread_mutex_lock (&pipe->lock);
	  err = pipe_wait_readable (pipe, 1, 1);
	  if (err == EWOULDBLOCK)
	    err = 0; /* Not readable, actually not an error.  */
	  else
	    ready |= SELECT_READ; /* Data immediately readable (or error).  */
	  pthread_mutex_unlock (&pipe->lock);
	}
      else
	{
	  ready |= SELECT_READ;	/* Error immediately available...  */
	}
      if (err)
	/* Prevent write test from overwriting err.  */
	*select_type &= ~SELECT_WRITE;
    }

  if (*select_type & SELECT_WRITE)
    {
      if (cred->po->openmodes & O_WRITE)
	{
	  pthread_mutex_lock (&pipe->lock);
	  err = pipe_wait_writable (pipe, 1);
	  if (err == EWOULDBLOCK)
	    err = 0; /* Not writable, actually not an error.  */
	  else
	    ready |= SELECT_WRITE; /* Data immediately writable (or error).  */
	  pthread_mutex_unlock (&pipe->lock);
	}
      else
	{
	  ready |= SELECT_WRITE;	/* Error immediately available...  */
	}
    }

  if (ready)
    *select_type = ready;
  else
    /* Wait for something to change.  */
    {
      ports_interrupt_self_on_port_death (cred, reply);
      err = pipe_pair_select (pipe, pipe, tsp, select_type, 1);
    }

  return err;
}

error_t
trivfs_S_io_select (struct trivfs_protid *cred,
		    mach_port_t reply, mach_msg_type_name_t reply_type,
		    int *select_type)
{
  return io_select_common (cred, reply, reply_type, NULL, select_type);
}

error_t
trivfs_S_io_select_timeout (struct trivfs_protid *cred,
			    mach_port_t reply, mach_msg_type_name_t reply_type,
			    struct timespec ts,
			    int *select_type)
{
  return io_select_common (cred, reply, reply_type, &ts, select_type);
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
		   data_t data, size_t data_len,
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
      pthread_mutex_lock (&pipe->lock);
      err = pipe_write (pipe, cred->po->openmodes & O_NONBLOCK, NULL,
			data, data_len, amount);
      pthread_mutex_unlock (&pipe->lock);
    }

  return err;
}

/* ---------------------------------------------------------------- */

/* Truncate file.  */
error_t
trivfs_S_file_set_size (struct trivfs_protid *cred,
			mach_port_t reply, mach_msg_type_name_t reply_type,
			off_t size)
{
  return size == 0 ? 0 : EINVAL;
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

/* ---------------------------------------------------------------- */

/* Ask SERVER to provide fsys translation service for us.  REQUESTOR is
   the bootstrap port supplied to the original translator, and ARGV are
   the command line arguments.  If the recipient accepts the request, he
   (or some delegate) should send fsys_startup to REQUESTOR to start the
   filesystem up.  */
error_t
trivfs_S_fsys_forward (mach_port_t server,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       mach_port_t requestor,
		       data_t argz, size_t argz_len)
{
  error_t err;
  struct fifo_trans *server_trans, *trans;
  int argc = argz_count (argz, argz_len);
  char **argv = alloca (sizeof (char *) * (argc + 1));
  /* SERVER should be our root node.  */
  struct trivfs_protid *cred =
    ports_lookup_port (port_bucket, server, server_port_class);

  if (!cred)
    return EOPNOTSUPP;

  server_trans = cred->po->cntl->hook;
  assert_backtrace (server_trans->server);

  argz_extract (argz, argz_len, argv);

  /* Make a new translator, inheriting from its server.  */
  fifo_trans_create (server_trans, &trans);

  /* Parse the new arguments to change the defaults.  */
  err = fifo_trans_parse_args (trans, argc, argv, 0);

  if (!err)
    /* Set our new translator along it's merry way... */
    fifo_trans_start (trans, requestor);

  ports_port_deref (cred);

  return err;
}
