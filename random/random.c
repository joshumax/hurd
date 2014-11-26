/* random.c - A single-file translator providing random data
   Copyright (C) 1998, 1999, 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#define _GNU_SOURCE 1

#include <hurd/trivfs.h>
#include <hurd/startup.h>
#include <stdio.h>
#include <stdlib.h>
#include <argp.h>
#include <argz.h>
#include <error.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <assert.h>

#include <version.h>

#include "random.h"
#include "gnupg-random.h"

/* Our control port.  */
struct trivfs_control *fsys;

int read_blocked;		/* For read and select.  */
pthread_cond_t wait;		/* For read and select.  */
pthread_cond_t select_alert;	/* For read and select.  */


/* The quality of randomness we provide.
   0: Very weak randomness based on time() and getrusage().
   No external random data is used.
   1: Pseudo random numbers based on all available real random
   numbers.
   2: Strong random numbers with a somewhat guaranteed entropy.
*/
#define DEFAULT_LEVEL 2
static int level = DEFAULT_LEVEL;

/* Name of file to use as seed.  */
static char *seed_file;

/* The random bytes we collected.  */
char gatherbuf[GATHERBUFSIZE];

/* The current positions in gatherbuf[].  */
int gatherrpos;
int gatherwpos;

/* XXX Yuk Yuk.  */
#define POOLSIZE 600

/* Take up to length bytes from gather_random if available.  If
   nothing is available, sleep until something becomes available.
   Must be called with global_lock held.  */
int
gather_random( void (*add)(const void*, size_t, int), int requester,
               size_t length, int level )
{
  int avail = (gatherwpos - gatherrpos + GATHERBUFSIZE) % GATHERBUFSIZE;
  int first = GATHERBUFSIZE - gatherrpos;
  int second = length - first;

  /* If level is zero, we should not block and not add anything
     to the pool.  */
  if( !level )
    return 0;

  /* io_read() should guarantee that there is always data available.  */
  if (level == 2)
    assert (avail);

  if (length > avail)
    length = avail;

  if (first > length)
    first = length;
  (*add) (&gatherbuf[gatherrpos], first, requester);
  gatherrpos = (gatherrpos + first) % GATHERBUFSIZE;
  if (second > 0)
    {
      (*add) (&gatherbuf[gatherrpos], second, requester);
      gatherrpos += second;
    }
  return length;
}


const char *argp_program_version = STANDARD_HURD_VERSION (random);

/* This lock protects the GnuPG code.  */
static pthread_mutex_t global_lock;

/* Trivfs hooks. */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;

int trivfs_allow_open = O_READ | O_WRITE;

int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 0;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  /* Mark the node as a read-only plain file. */
  st->st_mode &= ~S_IFMT;
  st->st_mode |= (S_IFCHR);
  st->st_size = 0;
}

error_t
trivfs_goaway (struct trivfs_control *cntl, int flags)
{
  update_random_seed_file ();
  exit (0);
}

/* Read data from an IO object.  If offset is -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in AMOUNT.  */
error_t
trivfs_S_io_read (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  data_t *data, mach_msg_type_number_t *data_len,
		  loff_t offs, mach_msg_type_number_t amount)
{
  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_READ))
    return EBADF;

  pthread_mutex_lock (&global_lock);

  if (amount > 0)
    {
      mach_msg_type_number_t new_amount;
      while (readable_pool (amount, level) == 0)
	{
	  if (cred->po->openmodes & O_NONBLOCK)
	    {
	      pthread_mutex_unlock (&global_lock);
	      return EWOULDBLOCK;
	    }
	  read_blocked = 1;
	  if (pthread_hurd_cond_wait_np (&wait, &global_lock))
	    {
	      pthread_mutex_unlock (&global_lock);
	      return EINTR;
	    }
	  /* See term/users.c for possible race?  */
	}

      /* Possibly allocate a new buffer. */
      if (*data_len < amount)
	{
	  *data = mmap (0, amount, PROT_READ|PROT_WRITE,
				       MAP_ANON, 0, 0);
	  if (*data == MAP_FAILED)
	    {
	      pthread_mutex_unlock (&global_lock);
	      return errno;
	    }
	}

      new_amount = read_pool ((byte *) *data, amount, level);

      if (new_amount < amount)
	munmap (*data + round_page (new_amount),
	        round_page(amount) - round_page (new_amount));
      amount = new_amount;
    }
  *data_len = amount;

  /* Set atime, see term/users.c */

  pthread_mutex_unlock (&global_lock);

  return 0;
}

/* Write data to an IO object.  If offset is -1, write at the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount successfully written is returned in amount.  A
   given user should not have more than one outstanding io_write on an
   object at a time; servers implement congestion control by delaying
   responses to io_write.  Servers may drop data (returning ENOBUFS)
   if they receive more than one write when not prepared for it.  */
error_t
trivfs_S_io_write (struct trivfs_protid *cred,
                   mach_port_t reply,
                   mach_msg_type_name_t replytype,
                   data_t data,
                   mach_msg_type_number_t datalen,
                   loff_t offset,
                   mach_msg_type_number_t *amount)
{
  int i = 0;
  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_WRITE))
    return EBADF;

  pthread_mutex_lock (&global_lock);

  while (i < datalen)
    {
      gatherbuf[gatherwpos] = data[i++];
      gatherwpos = (gatherwpos + 1) % GATHERBUFSIZE;
      if (gatherrpos == gatherwpos)
	/* Overrun.  */
	gatherrpos = (gatherrpos + 1) % GATHERBUFSIZE;
    }
  *amount = datalen;

  if (datalen > 0 && read_blocked)
    {
      read_blocked = 0;
      pthread_cond_broadcast (&wait);
      pthread_cond_broadcast (&select_alert);
    }

  pthread_mutex_unlock (&global_lock);
  return 0;
}

/* Tell how much data can be read from the object without blocking for
   a "long time" (this should be the same meaning of "long time" used
   by the nonblocking flag.  */
kern_return_t
trivfs_S_io_readable (struct trivfs_protid *cred,
                      mach_port_t reply, mach_msg_type_name_t replytype,
                      mach_msg_type_number_t *amount)
{
  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_READ))
    return EBADF;

  pthread_mutex_lock (&global_lock);

  /* XXX: Before initialization, the amount depends on the amount we
     want to read.  Assume some medium value.  */
  *amount = readable_pool (POOLSIZE/2, level);

  pthread_mutex_unlock (&global_lock);

  return 0;
}

/* SELECT_TYPE is the bitwise OR of SELECT_READ, SELECT_WRITE, and SELECT_URG.
   Block until one of the indicated types of i/o can be done "quickly", and
   return the types that are then available.  ID_TAG is returned as passed; it
   is just for the convenience of the user in matching up reply messages with
   specific requests sent.  */
error_t
trivfs_S_io_select (struct trivfs_protid *cred,
                    mach_port_t reply,
                    mach_msg_type_name_t reply_type,
                    int *type)
{
  if (!cred)
    return EOPNOTSUPP;

  /* We only deal with SELECT_READ and SELECT_WRITE here.  */
  if (*type & ~(SELECT_READ | SELECT_WRITE))
    return EINVAL;

  if (*type == 0)
    return 0;

    pthread_mutex_lock (&global_lock);

    while (1)
      {
	/* XXX Before initialization, readable_pool depends on length.  */
	int avail = readable_pool (POOLSIZE/2, level);

	if (avail != 0 || *type & SELECT_WRITE)
	  {
	    *type = (avail ? SELECT_READ : 0) | (*type & SELECT_WRITE);
	    pthread_mutex_unlock (&global_lock);
	    return 0;
	  }

	ports_interrupt_self_on_port_death (cred, reply);
	read_blocked = 1;

	if (pthread_hurd_cond_wait_np (&select_alert, &global_lock))
	  {
	    *type = 0;
	    pthread_mutex_unlock (&global_lock);
	    return EINTR;
	  }
      }
}


/* Change current read/write offset */
error_t
trivfs_S_io_seek (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  loff_t offs, int whence, loff_t *new_offs)
{
  if (! cred)
    return EOPNOTSUPP;

  /* Not seekable.  */
  return ESPIPE;
}

/* Change the size of the file.  If the size increases, new blocks are
   zero-filled.  After successful return, it is safe to reference mapped
   areas of the file up to NEW_SIZE.  */
error_t
trivfs_S_file_set_size (struct trivfs_protid *cred,
                        mach_port_t reply, mach_msg_type_name_t reply_type,
                        loff_t size)
{
  if (!cred)
    return EOPNOTSUPP;

  return size == 0 ? 0 : EINVAL;
}

/* These four routines modify the O_APPEND, O_ASYNC, O_FSYNC, and
   O_NONBLOCK bits for the IO object. In addition, io_get_openmodes
   will tell you which of O_READ, O_WRITE, and O_EXEC the object can
   be used for.  The O_ASYNC bit affects icky async I/O; good async
   I/O is done through io_async which is orthogonal to these calls. */
error_t
trivfs_S_io_set_all_openmodes(struct trivfs_protid *cred,
                              mach_port_t reply,
                              mach_msg_type_name_t reply_type,
                              int mode)
{
  if (!cred)
    return EOPNOTSUPP;

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

  return 0;
}

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

  return EINVAL;
}

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
                       mach_port_t reply, mach_msg_type_name_t reply_type,
                mach_port_t *rdobj,
                mach_msg_type_name_t *rdtype,
                mach_port_t *wrobj,
                mach_msg_type_name_t *wrtype)
{
  if (!cred)
    return EOPNOTSUPP;

  return EINVAL;
}


int
random_demuxer (mach_msg_header_t *inp,
                mach_msg_header_t *outp)
{
  extern int startup_notify_server (mach_msg_header_t *, mach_msg_header_t *);

  return (trivfs_demuxer (inp, outp)
	  || startup_notify_server (inp, outp));
}


/* Options processing.  We accept the same options on the command line
   and from fsys_set_options.  */

static const struct argp_option options[] =
{
  {"weak",      'w', 0, 0, "Output weak pseudo random data"},
  {"fast",	'f', 0,	0, "Output cheap random data fast"},
  {"secure",    's', 0, 0, "Output cryptographically secure random"},
  {"seed-file", 'S', "FILE", 0, "Use FILE to remember the seed"},
  {0}
};

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    default:
      return ARGP_ERR_UNKNOWN;
    case ARGP_KEY_INIT:
    case ARGP_KEY_SUCCESS:
    case ARGP_KEY_ERROR:
      break;

    case 'w':
      {
	level = 0;
	break;
      }
    case 'f':
      {
	level = 1;
	break;
      }
    case 's':
      {
	level = 2;
	break;
      }
    case 'S':
      {
	seed_file = strdup (arg);
	set_random_seed_file (arg);
      }
    }
  return 0;
}

/* This will be called from libtrivfs to help construct the answer
   to an fsys_get_options RPC.  */
error_t
trivfs_append_args (struct trivfs_control *fsys,
		    char **argz, size_t *argz_len)
{
  error_t err = 0;
  char *opt;
  
  pthread_mutex_lock (&global_lock);
  switch (level)
    {
    case 0:
      {
	opt = "--weak";
	break;
      }
    case 1:
      {
	opt = "--fast";
	break;
      }
    default:
      {
	opt = "--secure";
	break;
      }
    }
  if (level != DEFAULT_LEVEL)
    err = argz_add (argz, argz_len, opt);

  if (!err && seed_file)
    {
      if (asprintf (&opt, "--seed-file=%s", seed_file) < 0)
	err = ENOMEM;
      else
	{
	  err = argz_add (argz, argz_len, opt);
	  free (opt);
	}
    }
  pthread_mutex_unlock (&global_lock);

  return err;
}

static struct argp random_argp =
{ options, parse_opt, 0,
  "A translator providing random output." };

/* Setting this variable makes libtrivfs use our argp to
   parse options passed in an fsys_set_options RPC.  */
struct argp *trivfs_runtime_argp = &random_argp;

struct port_class *shutdown_notify_class;

/* The system is going down; destroy all the extant port rights.  That
   will cause net channels and such to close promptly.  */
error_t
S_startup_dosync (mach_port_t handle)
{
  struct port_info *inpi = ports_lookup_port (fsys->pi.bucket, handle,
					      shutdown_notify_class);

  if (!inpi)
    return EOPNOTSUPP;

  update_random_seed_file ();
  return 0;
}

void
sigterm_handler (int signo)
{
  update_random_seed_file ();
  signal (SIGTERM, SIG_DFL);
  raise (SIGTERM);
}

void
arrange_shutdown_notification ()
{
  error_t err;
  mach_port_t initport, notify;
  process_t procserver;
  struct port_info *pi;

  shutdown_notify_class = ports_create_class (0, 0);

  signal (SIGTERM, sigterm_handler);

  /* Arrange to get notified when the system goes down,
     but if we fail for some reason, just silently give up.  No big deal. */

  err = ports_create_port (shutdown_notify_class, fsys->pi.bucket,
			   sizeof (struct port_info), &pi);
  if (err)
    return;

  procserver = getproc ();
  if (!procserver)
    return;

  err = proc_getmsgport (procserver, 1, &initport);
  mach_port_deallocate (mach_task_self (), procserver);
  if (err)
    return;

  notify = ports_get_send_right (pi);
  ports_port_deref (pi);
  startup_request_notification (initport, notify,
				MACH_MSG_TYPE_MAKE_SEND,
				program_invocation_short_name);
  mach_port_deallocate (mach_task_self (), notify);
  mach_port_deallocate (mach_task_self (), initport);
}


int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;

  /* Initialize the lock that will protect everything.
     We must do this before argp_parse, because parse_opt (above) will
     use the lock.  */
  pthread_mutex_init (&global_lock, NULL);

  /* The conditions are used to implement proper read/select
     behaviour.  */
  pthread_cond_init (&wait, NULL);
  pthread_cond_init (&select_alert, NULL);

  /* We use the same argp for options available at startup
     as for options we'll accept in an fsys_set_options RPC.  */
  argp_parse (&random_argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "trivfs_startup");

  arrange_shutdown_notification ();

  /* Launch. */
  ports_manage_port_operations_multithread (fsys->pi.bucket, random_demuxer,
					    10 * 1000, /* idle thread */
					    10 * 60 * 1000, /* idle server */
					    0);
  return 0;
}
