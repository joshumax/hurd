/* A translator for handling stream devices.

   Copyright (C) 2001,02 Free Software Foundation, Inc.

   Written by OKUJI Yoshinori <okuji@kuicr.kyoto-u.ac.jp>

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

#include <string.h>
#include <assert-backtrace.h>
#include <stdio.h>
#include <fcntl.h>
#include <argp.h>
#include <error.h>

#include <mach.h>
#include <device/device.h>
#include <device/device_request.h>

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <version.h>

#include "libtrivfs/trivfs_fs_S.h"
#include "libtrivfs/trivfs_io_S.h"
#include "device_reply_S.h"

/* The global lock */
pthread_mutex_t global_lock;

/* Wakeup when device_open is finished */
pthread_cond_t open_alert;

/* Wakeup for select */
pthread_cond_t select_alert;

/* Bucket for all out ports */
struct port_bucket *streamdev_bucket;

/* The buffers we use */
struct buffer *input_buffer, *output_buffer;


/* Information about a buffer.  */
struct buffer
{
  /* Point to the head of the buffer.  */
  char *head;
  /* Point to the tail of the buffer.  */
  char *tail;
  /* The buffer array size.  */
  size_t size;
  /* Wakeup when the buffer is not empty or not full.  */
  pthread_cond_t *wait;
  /* The buffer.  */
  char buf[0];
};

/* Create a new buffer structure with SIZE, returning the pointer.  */
static inline struct buffer *
create_buffer (size_t size)
{
  struct buffer *new = malloc (sizeof (struct buffer) + size);
  assert_backtrace (new);
  new->head = new->tail = new->buf;
  new->size = size;
  new->wait = malloc (sizeof (pthread_cond_t));
  assert_backtrace (new->wait);
  pthread_cond_init (new->wait, NULL);
  return new;
}

/* Return the size of B.  */
static inline size_t
buffer_size (struct buffer *b)
{
  return b->tail - b->head;
}

/* Return how much characters can be read from B.  */
static inline size_t
buffer_readable (struct buffer *b)
{
  return buffer_size (b);
}

/* Return how much characters can be written to B.  */
static inline size_t
buffer_writable (struct buffer *b)
{
  return b->size - buffer_size (b);
}

/* Flush B.  */
static inline void
clear_buffer (struct buffer *b)
{
  if (b == 0)
    return;
  b->head = b->tail = b->buf;
  pthread_cond_broadcast (b->wait);
  pthread_cond_broadcast (&select_alert);
}

/* Read up to LEN bytes from B to DATA, returning the amount actually read.  */
static inline size_t
buffer_read (struct buffer *b, void *data, size_t len)
{
  size_t max = buffer_size (b);

  if (len > max)
    len = max;

  memcpy (data, b->head, len);
  b->head += len;

  if (b->head > b->buf + b->size / 2)
    {
      size_t size = buffer_size (b);

      memmove (b->buf, b->head, size);
      b->head = b->buf;
      b->tail = b->buf + size;
    }

  pthread_cond_broadcast (b->wait);
  pthread_cond_broadcast (&select_alert);
  return len;
}

/* Write LEN bytes from DATA to B, returning the amount actually written.  */
static inline size_t
buffer_write (struct buffer *b, void *data, size_t len)
{
  size_t size = buffer_writable (b);

  if (len > size)
    len = size;

  memcpy (b->tail, data, len);
  b->tail += len;

  pthread_cond_broadcast (b->wait);
  pthread_cond_broadcast (&select_alert);
  return len;
}


/* Open a new device structure for the device NAME with MODE. If an error
   occurs, the error code is returned, otherwise 0.  */
error_t dev_open (const char *name, dev_mode_t mode);

/* Check if the device is already opened.  */
int dev_already_opened (void);

/* Close the device.  */
void dev_close (void);

/* Read up to AMOUNT bytes, returned in BUF and LEN. If NOWAIT is non-zero
   and the buffer is empty, then returns EWOULDBLOCK. If an error occurs,
   the error code is returned, otherwise 0.  */
error_t dev_read (size_t amount, void **buf, size_t *len, int nowait);

/* Return current readable size in AMOUNT. If an error occurs, the error
   code is returned, otherwise 0.  */
error_t dev_readable (size_t *amount);

/* Write LEN bytes from BUF, returning the amount actually written
   in AMOUNT. If NOWAIT is non-zero and the buffer is full, then returns
   EWOULDBLOCK. If an error occurs, the error code is returned,
   otherwise 0.  */
error_t dev_write (void *buf, size_t len, size_t *amount, int nowait);

/* Try and write out any pending writes to the device. If WAIT is non-zero,
   will wait for any activity to cease.  */
error_t dev_sync (int wait);



static struct argp_option options[] =
{
  {"rdev",     'n', "ID", 0,
   "The stat rdev number for this node; may be either a"
   " single integer, or of the form MAJOR,MINOR"},
  {"readonly", 'r', 0,    0, "Disallow writing"},
  {"rdonly",   0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"ro",       0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"writable", 'w', 0,    0, "Allow writing"},
  {"rdwr",     0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"rw",       0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"writeonly", 'W',0,    0, "Disallow reading"},
  {"wronly",   0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {0}
};

static const char args_doc[] = "DEVICE";
static const char doc[] = "Translator for stream devices.";

const char *argp_program_version = STANDARD_HURD_VERSION (streamio);


static char *stream_name;
static int rdev;
static int nperopens;

/* Parse a single option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'r':
      trivfs_allow_open = O_READ;
      break;
    case 'w':
      trivfs_allow_open = O_RDWR;
      break;
    case 'W':
      trivfs_allow_open = O_WRITE;
      break;

    case 'n':
      {
	char *start = arg;
	char *end;

	rdev = strtoul (start, &end, 0);
	if (*end == ',')
	  /* MAJOR,MINOR form */
	  {
	    start = end + 1;
	    rdev = (rdev << 8) + strtoul (start, &end, 0);
	  }

	if (end == start || *end != '\0')
	  {
	    argp_error (state, "%s: Invalid argument to --rdev", arg);
	    return EINVAL;
	  }
      }
      break;

    case ARGP_KEY_ARG:
      stream_name = arg;
      break;

    case ARGP_KEY_END:
      if (stream_name == 0)
	argp_usage (state);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp argp = { options, parse_opt, args_doc, doc };


int
demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = NULL, trivfs_demuxer (inp, outp)) ||
      (routine = device_reply_server_routine (inp)))
    {
      if (routine)
        (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

int
main (int argc, char *argv[])
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;

  argp_parse (&argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (2, 0, "Must be started as a translator");

  streamdev_bucket = ports_create_bucket ();

  err = trivfs_startup (bootstrap, 0,
			0, streamdev_bucket, 0, streamdev_bucket,
			&fsys);
  if (err)
    error (3, err, "trivfs_startup");

  pthread_mutex_init (&global_lock, NULL);

  pthread_cond_init (&open_alert, NULL);
  pthread_cond_init (&select_alert, NULL);

  if (trivfs_allow_open & O_READ)
    input_buffer = create_buffer (256);
  if (trivfs_allow_open & O_WRITE)
    output_buffer = create_buffer (256);

  /* Launch */
  ports_manage_port_operations_multithread (streamdev_bucket, demuxer,
					    0, 0, 0);

  return 0;
}


int trivfs_fstype = FSTYPE_DEV;
int trivfs_fsid = 0;

int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ | O_WRITE;

static error_t
open_hook (struct trivfs_control *cntl, struct iouser *user, int flags)
{
  error_t err;
  dev_mode_t mode;

  if (flags & O_WRITE & ~trivfs_allow_open)
    return EROFS;
  if (flags & O_READ & ~trivfs_allow_open)
    return EIO;

  if ((flags & (O_READ|O_WRITE)) == 0)
    return 0;

  /* XXX */
  if (flags & O_ASYNC)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);

  mode = 0;
  if (flags & O_READ)
    mode |= D_READ;
  if (flags & O_WRITE)
    mode |= D_WRITE;

  if (!dev_already_opened ())
    {
      err = dev_open (stream_name, mode);
      if (err)
	{
	  pthread_mutex_unlock (&global_lock);
	  return err;
	}

      if (!(flags & O_NONBLOCK))
	{
	  if (pthread_hurd_cond_wait_np (&open_alert, &global_lock))
	    {
	      pthread_mutex_unlock (&global_lock);
	      return EINTR;
	    }

	  if (!dev_already_opened ())
	    {
	      pthread_mutex_unlock (&global_lock);
	      return ENODEV;
	    }
	}
    }

  pthread_mutex_unlock (&global_lock);
  return 0;
}

error_t (*trivfs_check_open_hook) (struct trivfs_control *,
				   struct iouser *, int)
     = open_hook;

static error_t
po_create_hook (struct trivfs_peropen *po)
{
  pthread_mutex_lock (&global_lock);
  nperopens++;
  pthread_mutex_unlock (&global_lock);
  return 0;
}

error_t (*trivfs_peropen_create_hook) (struct trivfs_peropen *) =
     po_create_hook;

static void
po_destroy_hook (struct trivfs_peropen *po)
{
  pthread_mutex_lock (&global_lock);
  nperopens--;
  if (!nperopens)
    {
      if (dev_already_opened ())
	{
	  clear_buffer (input_buffer);
	  dev_close ();
	}
    }
  pthread_mutex_unlock (&global_lock);
}

void (*trivfs_peropen_destroy_hook) (struct trivfs_peropen *)
     = po_destroy_hook;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_blksize = vm_page_size;
  st->st_size = 0;

  st->st_rdev = rdev;
  st->st_mode &= ~S_IFMT;
  st->st_mode |= S_IFCHR;
  if ((trivfs_allow_open & O_READ) == 0)
    st->st_mode &= ~(S_IRUSR | S_IRGRP | S_IROTH);
  if ((trivfs_allow_open & O_WRITE) == 0)
    st->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  error_t err;
  int force = (flags & FSYS_GOAWAY_FORCE);
  int nosync = (flags & FSYS_GOAWAY_NOSYNC);
  struct port_class *root_port_class = fsys->protid_class;

  pthread_mutex_lock (&global_lock);

  if (!dev_already_opened ())
    exit (0);

  err = ports_inhibit_class_rpcs (root_port_class);
  if (err == EINTR || (err && !force))
    {
      pthread_mutex_unlock (&global_lock);
      return err;
    }

  if (force && nosync)
    exit (0);

  if (!force && ports_count_class (root_port_class) > 0)
    goto busy;

  if (!nosync)
    dev_close ();
  exit (0);

 busy:
  ports_enable_class (root_port_class);
  ports_resume_class_rpcs (root_port_class);
  pthread_mutex_unlock (&global_lock);

  return EBUSY;
}


error_t
trivfs_S_io_read (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  data_t *data, mach_msg_type_number_t *data_len,
		  loff_t offs, mach_msg_type_number_t amount)
{
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  if (!(cred->po->openmodes & O_READ))
    return EBADF;

  pthread_mutex_lock (&global_lock);
  err = dev_read (amount, (void **)data, data_len, cred->po->openmodes & O_NONBLOCK);
  pthread_mutex_unlock (&global_lock);
  return err;
}

error_t
trivfs_S_io_readable (struct trivfs_protid *cred,
		      mach_port_t reply, mach_msg_type_name_t reply_type,
		      mach_msg_type_number_t *amount)
{
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  if (!(cred->po->openmodes & O_READ))
    return EBADF;

  pthread_mutex_lock (&global_lock);
  err = dev_readable (amount);
  pthread_mutex_unlock (&global_lock);
  return err;
}

error_t
trivfs_S_io_write (struct trivfs_protid *cred,
		   mach_port_t reply, mach_msg_type_name_t reply_type,
		   data_t data, mach_msg_type_number_t data_len,
		   loff_t offs, mach_msg_type_number_t *amount)
{
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  if (!(cred->po->openmodes & O_WRITE))
    return EBADF;

  pthread_mutex_lock (&global_lock);
  err = dev_write ((void *)data, data_len, amount, cred->po->openmodes & O_NONBLOCK);
  pthread_mutex_unlock (&global_lock);
  return err;
}

error_t
trivfs_S_io_seek (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  off_t offs, int whence, off_t *new_offs)
{
  if (!cred)
    return EOPNOTSUPP;
  else
    return ESPIPE;
}

static error_t
io_select_common (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  struct timespec *tsp,
		  int *type)
{
  int available;
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  *type &= SELECT_READ | SELECT_WRITE;

  if (*type == 0)
    return 0;

  available = 0;

  while (1)
    {
      pthread_mutex_lock (&global_lock);
      if ((*type & SELECT_READ) && buffer_readable (input_buffer))
	available |= SELECT_READ;
      if ((*type & SELECT_WRITE) &&
	  (!(cred->po->openmodes & O_WRITE) ||
	    (buffer_writable (output_buffer))))
	available |= SELECT_WRITE;

      if (available)
	{
	  *type = available;
	  pthread_mutex_unlock (&global_lock);
	  return 0;
	}

      if (cred->po->openmodes & O_NONBLOCK)
	{
	  pthread_mutex_unlock (&global_lock);
	  return EWOULDBLOCK;
	}

      ports_interrupt_self_on_port_death (cred, reply);
      err = pthread_hurd_cond_timedwait_np (&select_alert, &global_lock, tsp);
      if (err)
	{
	  *type = 0;
	  pthread_mutex_unlock (&global_lock);

	  if (err == ETIMEDOUT)
	    err = 0;

	  return err;
	}
    }
}

error_t
trivfs_S_io_select (struct trivfs_protid *cred,
		    mach_port_t reply, mach_msg_type_name_t reply_type,
		    int *type)
{
  return io_select_common (cred, reply, reply_type, NULL, type);
}

error_t
trivfs_S_io_select_timeout (struct trivfs_protid *cred,
			    mach_port_t reply, mach_msg_type_name_t reply_type,
			    struct timespec ts,
			    int *type)
{
  return io_select_common (cred, reply, reply_type, &ts, type);
}

error_t
trivfs_S_file_set_size (struct trivfs_protid *cred,
			mach_port_t reply, mach_msg_type_name_t reply_type,
			off_t size)
{
  if (!cred)
    return EOPNOTSUPP;
  else if (!(cred->po->openmodes & O_WRITE))
    return EBADF;
  else if (size < 0)
    return EINVAL;
  else
    return 0;
}

error_t
trivfs_S_io_get_openmodes (struct trivfs_protid *cred,
			   mach_port_t reply, mach_msg_type_name_t reply_type,
			   int *bits)
{
  if (! cred)
    return EOPNOTSUPP;
  else
    {
      *bits = cred->po->openmodes;
      return 0;
    }
}

error_t
trivfs_S_io_set_all_openmodes (struct trivfs_protid *cred,
			       mach_port_t reply,
			       mach_msg_type_name_t reply_type,
			       int mode)
{
  if (! cred)
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
  if (! cred)
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
  if (! cred)
    return EOPNOTSUPP;
  else
    return 0;
}

error_t
trivfs_S_file_sync (struct trivfs_protid *cred,
		    mach_port_t reply, mach_msg_type_name_t reply_type,
		    int wait, int omit_metadata)
{
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  err = dev_sync (wait);
  pthread_mutex_unlock (&global_lock);
  return err;
}

error_t
trivfs_S_file_syncfs (struct trivfs_protid *cred,
		      mach_port_t reply, mach_msg_type_name_t reply_type,
		      int wait, int dochildren)
{
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  err = dev_sync (wait);
  pthread_mutex_unlock (&global_lock);
  return err;
}


/* This flag is set if there is an outstanding device_write.  */
static int output_pending;

/* This flag is set if there is an outstanding device_read.  */
static int input_pending;

/* This flag is set if there is an outstanding device_open.  */
static int open_pending;

static char pending_output[IO_INBAND_MAX];
static int npending_output;

/* This flag is set if EOF is returned.  */
static int eof;

/* The error number.  */
static error_t err;

static struct port_class *phys_reply_class;

/* The Mach device_t representing the stream.  */
static device_t phys_device = MACH_PORT_NULL;

/* The ports we get replies on for device calls.  */
static mach_port_t phys_reply_writes = MACH_PORT_NULL;
static mach_port_t phys_reply = MACH_PORT_NULL;

/* The port-info structures.  */
static struct port_info *phys_reply_writes_pi;
static struct port_info *phys_reply_pi;

static device_t device_master;

/* The block size and whole size of the device.  */
static size_t dev_blksize;
static size_t dev_size;


/* Open a new device structure for the device NAME with MODE. If an error
   occurs, the error code is returned, otherwise 0.  */
/* Be careful that the global lock is already locked.  */
error_t
dev_open (const char *name, dev_mode_t mode)
{
  if (open_pending || (phys_device != MACH_PORT_NULL))
    return 0;

  err = get_privileged_ports (0, &device_master);
  if (err)
    return err;

  phys_reply_class = ports_create_class (0, 0);
  err = ports_create_port (phys_reply_class, streamdev_bucket,
			   sizeof (struct port_info), &phys_reply_pi);
  if (err)
    {
      mach_port_deallocate (mach_task_self (), device_master);
      return err;
    }

  phys_reply = ports_get_right (phys_reply_pi);
  mach_port_insert_right (mach_task_self (), phys_reply, phys_reply,
			  MACH_MSG_TYPE_MAKE_SEND);

  if (output_buffer)
    {
      err = ports_create_port (phys_reply_class, streamdev_bucket,
			       sizeof (struct port_info),
			       &phys_reply_writes_pi);
      if (err)
	{
	  mach_port_deallocate (mach_task_self (), phys_reply);
	  phys_reply = MACH_PORT_NULL;
	  ports_port_deref (phys_reply_pi);
	  phys_reply_pi = 0;
	  mach_port_deallocate (mach_task_self (), device_master);
	  return err;
	}

      phys_reply_writes = ports_get_right (phys_reply_writes_pi);
      mach_port_insert_right (mach_task_self (), phys_reply_writes,
			      phys_reply_writes, MACH_MSG_TYPE_MAKE_SEND);
    }

  err = device_open_request (device_master, phys_reply, mode, (char *) name);
  if (err)
    {
      mach_port_deallocate (mach_task_self (), phys_reply);
      phys_reply = MACH_PORT_NULL;
      ports_port_deref (phys_reply_pi);
      phys_reply_pi = 0;
      if (output_buffer)
	{
	  mach_port_deallocate (mach_task_self (), phys_reply_writes);
	  phys_reply_writes = MACH_PORT_NULL;
	  ports_port_deref (phys_reply_writes_pi);
	  phys_reply_writes_pi = 0;
	}
      mach_port_deallocate (mach_task_self (), device_master);
      return err;
    }

  open_pending = 1;
  return 0;
}

kern_return_t
device_open_reply (mach_port_t reply, int returncode, mach_port_t device)
{
  int sizes[DEV_GET_SIZE_COUNT];
  size_t sizes_len = DEV_GET_SIZE_COUNT;
  int amount;

  if (reply != phys_reply)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);

  open_pending = 0;
  pthread_cond_broadcast (&open_alert);

  if (returncode != 0)
    {
      dev_close ();
      pthread_mutex_unlock (&global_lock);
      return 0;
    }

  phys_device = device;
  eof = 0;

  /* Get the block size and the whole size.  */
  err = device_get_status (device, DEV_GET_SIZE, sizes, &sizes_len);
  if (err == D_INVALID_OPERATION)
    {
      /* XXX Assume that the block size is 1 and the whole size is 0.  */
      dev_blksize = 1;
      dev_size = 0;
      err = 0;
    }
  else if (err == 0)
    {
      assert_backtrace (sizes_len == DEV_GET_SIZE_COUNT);

      dev_blksize = sizes[DEV_GET_SIZE_RECORD_SIZE];
      dev_size = sizes[DEV_GET_SIZE_DEVICE_SIZE];

      assert_backtrace (dev_blksize && dev_blksize <= IO_INBAND_MAX);
    }
  else
    {
      dev_close ();
      pthread_mutex_unlock (&global_lock);
      return 0;
    }

  amount = vm_page_size;
  if (dev_blksize != 1)
    amount = amount / dev_blksize * dev_blksize;

  pthread_mutex_unlock (&global_lock);
  return 0;
}

/* Check if the device is already opened.  */
/* Be careful that the global lock is already locked.  */
int
dev_already_opened (void)
{
  return (phys_device != MACH_PORT_NULL);
}

/* Close the device.  */
/* Be careful that the global lock is already locked.  */
void
dev_close (void)
{
  /* Sync all pending writes.  */
  dev_sync (1);

  device_close (phys_device);
  mach_port_deallocate (mach_task_self (), phys_device);
  phys_device = MACH_PORT_NULL;

  mach_port_deallocate (mach_task_self (), phys_reply);
  phys_reply = MACH_PORT_NULL;
  ports_port_deref (phys_reply_pi);
  phys_reply_pi = 0;
  clear_buffer (input_buffer);
  input_pending = 0;

  if (output_buffer)
    {
      mach_port_deallocate (mach_task_self (), phys_reply_writes);
      phys_reply_writes = MACH_PORT_NULL;
      ports_port_deref (phys_reply_writes_pi);
      phys_reply_writes_pi = 0;
      clear_buffer (output_buffer);
      npending_output = 0;
      output_pending = 0;
    }
}

/* Be careful that the global lock is already locked.  */
static error_t
start_input (int nowait)
{
  int size;
  error_t err;
  size_t amount;

  size = buffer_writable (input_buffer);

  if (size < dev_blksize || input_pending)
    return 0;

  amount = vm_page_size;
  if (dev_blksize != 1)
    amount = amount / dev_blksize * dev_blksize;

  err = device_read_request_inband (phys_device, phys_reply,
				    nowait? D_NOWAIT : 0,
				    0, amount);
  if (err == D_WOULD_BLOCK)
    err = 0;
  if (err)
    dev_close ();
  else
    input_pending = 1;

  return err;
}

/* Read up to AMOUNT bytes, returned in BUF and LEN. If NOWAIT is non-zero
   and the buffer is empty, then returns EWOULDBLOCK. If an error occurs,
   the error code is returned, otherwise 0.  */
/* Be careful that the global lock is already locked.  */
error_t
dev_read (size_t amount, void **buf, size_t *len, int nowait)
{
  size_t max, avail;

  if (err)
    return err;

  while (!buffer_readable (input_buffer))
    {
      err = start_input (nowait);
      if (err)
	return err;

      if (eof)
	{
	  *len = 0;
	  return 0;
	}

      if (nowait)
	return EWOULDBLOCK;

      if (pthread_hurd_cond_wait_np (input_buffer->wait, &global_lock))
	return EINTR;
    }

  avail = buffer_size (input_buffer);
  max = (amount < avail) ? amount : avail;
  if (max > *len)
    vm_allocate (mach_task_self (), (vm_address_t *)buf, max, 1);

  *len = buffer_read (input_buffer, *buf, max);
  assert_backtrace (*len == max);

  err = start_input (nowait);
  return err;
}

error_t
device_read_reply_inband (mach_port_t reply, error_t errorcode,
			  char *data, u_int datalen)
{
  if (reply != phys_reply)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);

  input_pending = 0;
  err = errorcode;
  if (!err)
    {
      if (datalen == 0)
	{
	  eof = 1;
	  dev_close ();
	  pthread_mutex_unlock (&global_lock);
	  return 0;
	}

      while (datalen)
	{
	  size_t nwritten;

	  while (!buffer_writable (input_buffer))
	    pthread_cond_wait (input_buffer->wait, &global_lock);

	  nwritten = buffer_write (input_buffer, data, datalen);
	  data += nwritten;
	  datalen -= nwritten;
	  pthread_cond_broadcast (input_buffer->wait);
	  pthread_cond_broadcast (&select_alert);
	}
    }
  else
    {
      dev_close ();
      pthread_mutex_unlock (&global_lock);
      return 0;
    }
  pthread_mutex_unlock (&global_lock);
  return 0;
}

/* Return current readable size in AMOUNT. If an error occurs, the error
   code is returned, otherwise 0.  */
/* Be careful that the global lock is already locked.  */
error_t
dev_readable (size_t *amount)
{
  *amount = buffer_size (input_buffer);
  return 0;
}

/* Be careful that the global lock is already locked.  */
static error_t
start_output (int nowait)
{
  int size;

  assert_backtrace (output_buffer);

  size = buffer_size (output_buffer);

  if (size < dev_blksize || output_pending)
    return 0;

  if (size + npending_output > IO_INBAND_MAX)
    size = IO_INBAND_MAX - npending_output;

  if (dev_blksize != 1)
    size = size / dev_blksize * dev_blksize;

  buffer_read (output_buffer, pending_output + npending_output, size);
  npending_output += size;

  err = device_write_request_inband (phys_device, phys_reply_writes,
				     nowait? D_NOWAIT : 0,
				     0, pending_output, npending_output);
  if (err == D_WOULD_BLOCK)
    err = 0;
  if (err)
    dev_close ();
  else
    output_pending = 1;

  return err;
}

/* Write LEN bytes from BUF, returning the amount actually written
   in AMOUNT. If NOWAIT is non-zero and the buffer is full, then returns
   EWOULDBLOCK. If an error occurs, the error code is returned,
   otherwise 0.  */
/* Be careful that the global lock is already locked.  */
error_t
dev_write (void *buf, size_t len, size_t *amount, int nowait)
{
  if (err)
    return err;

  while (!buffer_writable (output_buffer))
    {
      err = start_output (nowait);
      if (err)
	return err;

      if (nowait)
	return EWOULDBLOCK;

      if (pthread_hurd_cond_wait_np (output_buffer->wait, &global_lock))
	return EINTR;
    }

  *amount = buffer_write (output_buffer, buf, len);
  err = start_output (nowait);

  return err;
}

error_t
device_write_reply_inband (mach_port_t reply, error_t returncode, int amount)
{
  if (reply != phys_reply_writes)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);

  output_pending = 0;

  if (!returncode)
    {
      if (amount >= npending_output)
	{
	  npending_output = 0;
	  pthread_cond_broadcast (output_buffer->wait);
	  pthread_cond_broadcast (&select_alert);
	}
      else
	{
	  npending_output -= amount;
	  memmove (pending_output, pending_output + amount, npending_output);
	}
    }
  else
    dev_close ();

  pthread_mutex_unlock (&global_lock);
  return 0;
}

/* Try and write out any pending writes to the device. If WAIT is non-zero,
   will wait for any activity to cease.  */
/* Be careful that the global lock is already locked.  */
error_t
dev_sync (int wait)
{
  if (err)
    return err;

  if (!output_buffer || phys_device == MACH_PORT_NULL)
    return 0;

  while (buffer_readable (output_buffer) >= dev_blksize)
    {
      err = start_output (! wait);
      if (err)
	return err;

      if (!wait)
	return 0;

      if (pthread_hurd_cond_wait_np (output_buffer->wait, &global_lock))
	return EINTR;
    }

  /* XXX: When the size of output_buffer is non-zero and less than
     DEV_BLKSIZE, the rest will be ignored or discarded.  */
  return 0;
}

/* Unused stubs.  */
kern_return_t
device_read_reply (mach_port_t reply, kern_return_t returncode,
		   io_buf_ptr_t data, mach_msg_type_number_t amount)
{
  return EOPNOTSUPP;
}

kern_return_t
device_write_reply (mach_port_t reply, kern_return_t returncode, int amount)
{
  return EOPNOTSUPP;
}
