/* random.c - A single-file translator providing random data
   Copyright (C) 1998, 1999, 2001, 2017 Free Software Foundation, Inc.

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

#include <argp.h>
#include <argz.h>
#include <assert-backtrace.h>
#include <error.h>
#include <fcntl.h>
#include <gcrypt.h>
#include <hurd/paths.h>
#include <hurd/startup.h>
#include <hurd/trivfs.h>
#include <mach/gnumach.h>
#include <mach/vm_cache_statistics.h>
#include <mach/vm_param.h>
#include <mach/vm_statistics.h>
#include <mach_debug/mach_debug_types.h>
#include <maptime.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <version.h>

#include "mach_debug_U.h"



/* Entropy pool.  We use one of the SHAKE algorithms from the Keccak
   family.  Being a sponge construction, it allows the extraction of
   arbitrary amounts of pseudorandom data.  */
static gcry_md_hd_t pool;
enum gcry_md_algos hash_algo = GCRY_MD_SHAKE128;

/* Protected by this lock.  */
static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;

/* A map of the Mach time device.  Used for quick stirring.  */
volatile struct mapped_time_value *mtime;

static void
pool_initialize (void)
{
  error_t err;
  gcry_error_t cerr;

  if (! gcry_check_version ("1.8.0"))
    error (1, 0, "libgcrypt version mismatch\n");

  cerr = gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);
  if (cerr)
    error (1, 0, "Finalizing gcrypt failed: %s",
	   gcry_strerror (cerr));

  cerr = gcry_md_open (&pool, hash_algo, GCRY_MD_FLAG_SECURE);
  if (cerr)
    error (1, 0, "Initializing hash failed: %s",
	   gcry_strerror (cerr));

  err = maptime_map (0, NULL, &mtime);
  if (err)
    err = maptime_map (1, NULL, &mtime);
  if (err)
    error (1, err, "Failed to map time device");
}

/* Mix data into the pool.  */
static void
pool_add_entropy (const void *buffer, size_t length)
{
  pthread_mutex_lock (&pool_lock);
  gcry_md_write (pool, buffer, length);
  pthread_mutex_unlock (&pool_lock);
}

/* Extract data from the pool.  */
static error_t
pool_randomize (void *buffer, size_t length)
{
  gcry_error_t cerr;
  pthread_mutex_lock (&pool_lock);

  /* Quickly stir the the time device into the pool.  Do not even
     bother with synchronization.  */
  gcry_md_write (pool, (void *) mtime, sizeof *mtime);

  cerr = gcry_md_extract (pool, hash_algo, buffer, length);
  pthread_mutex_unlock (&pool_lock);
  return cerr ? EIO : 0;
}



/* Name of file to use as seed.  */
static char *seed_file;

/* Size of the seed file.  */
size_t seed_size = 600;

static error_t
update_random_seed_file (void)
{
  error_t err;
  int fd;
  void *map;

  if (seed_file == NULL)
    return 0;

  fd = open (seed_file, O_RDWR|O_CREAT, 0600);
  if (fd < 0)
    return errno;

  if (ftruncate (fd, seed_size))
    {
      err = errno;
      goto out;
    }

  map = mmap (NULL, seed_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED)
    {
      err = errno;
      goto out;
    }

  err = pool_randomize (map, seed_size);
  munmap (map, seed_size);

 out:
  close (fd);
  return err;
}

static error_t
read_random_seed_file (void)
{
  error_t err;
  int fd;
  struct stat s;
  void *map;

  if (seed_file == NULL)
    return 0;

  fd = open (seed_file, O_RDWR);
  if (fd < 0)
    return errno;

  if (fstat (fd, &s))
    {
      err = errno;
      goto out;
    }

  /* XXX should check file permissions.  */

  map = mmap (NULL, s.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED)
    {
      err = errno;
      goto out;
    }

  pool_add_entropy (map, s.st_size);
  /* Immediately update it, to minimize the chance that the same state
     is read twice.  */
  pool_randomize (map, s.st_size);
  munmap (map, s.st_size);

 out:
  close (fd);
  return err;
}



static void
gather_slab_info (void)
{
  error_t err;
  cache_info_array_t cache_info;
  mach_msg_type_number_t cache_info_count;

  cache_info = NULL;
  cache_info_count = 0;

  err = host_slab_info (mach_host_self(), &cache_info, &cache_info_count);
  if (err)
    return;

  pool_add_entropy (cache_info, cache_info_count * sizeof *cache_info);

  vm_deallocate (mach_task_self (),
		 (vm_address_t) cache_info,
		 cache_info_count * sizeof *cache_info);
}

static void
gather_vm_statistics (void)
{
  error_t err;
  struct vm_statistics vmstats;

  err = vm_statistics (mach_task_self (), &vmstats);
  if (err)
    return;

  pool_add_entropy (&vmstats, sizeof vmstats);
}

static void
gather_vm_cache_statistics (void)
{
  error_t err;
  struct vm_cache_statistics cache_stats;

  err = vm_cache_statistics (mach_task_self (), &cache_stats);
  if (err)
    return;

  pool_add_entropy (&cache_stats, sizeof cache_stats);
}

static void *
gather_thread (void *args)
{
  while (1)
    {
      gather_slab_info ();
      gather_vm_statistics ();
      gather_vm_cache_statistics ();
      usleep (
        (useconds_t) (1000000. * (1.
                                  + (float) random () / (float) RAND_MAX)));
    }

  assert_backtrace (! "reached");
}

error_t
start_gather_thread (void)
{
  error_t err;
  pthread_t thread;

  err = pthread_create (&thread, NULL, gather_thread, NULL);
  if (err)
    return err;

  err = pthread_detach (thread);
  return err;
}



const char *argp_program_version = STANDARD_HURD_VERSION (random);

/* Our control port.  */
struct trivfs_control *fsys;

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
  st->st_mode &= ~((unsigned) S_IFMT);
  st->st_mode |= (S_IFCHR);
  st->st_size = 0;
}

error_t
trivfs_goaway (struct trivfs_control *cntl, int flags)
{
  error_t err;
  err = update_random_seed_file ();
  if (err)
    error (0, err, "Warning: Failed to save random seed to %s", seed_file);
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
  error_t err;
  void *buf = NULL;
  size_t length = 0;

  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_READ))
    return EBADF;

  if (amount > 0)
    {
      /* Possibly allocate a new buffer. */
      if (*data_len < amount)
	{
	  *data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  if (*data == MAP_FAILED)
	    {
              err = errno;
              goto errout;
	    }

	  /* Keep track of our map in case of errors.  */
	  buf = *data, length = amount;

	  /* Update DATA_LEN to reflect the new buffers size.  */
	  *data_len = amount;
	}

      err = pool_randomize (*data, amount);
      if (err)
        goto errout;

    }

  *data_len = amount;
  trivfs_set_atime (fsys);
  return 0;

 errout:
  if (buf)
    munmap (buf, length);
  return err;
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
  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_WRITE))
    return EBADF;

  pool_add_entropy (data, datalen);
  *amount = datalen;
  trivfs_set_mtime (fsys);
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

  /* We allow an infinite amount of data to be extracted.  We need to
     return something here, so just go with the page size.  */
  *amount = PAGE_SIZE;
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

  /* We allow an infinite amount of data to be extracted and stored.
     Just return success.  */
  return 0;
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
  {"fast",	'f', 0,	0, "(ignored)"},
  {"secure",    's', 0, 0, "(ignored)"},
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

    case 'f':
    case 's':
      /* Ignored.  */
      break;

    case 'S':
      seed_file = strdup (arg);
      break;
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

  if (seed_file)
    {
      if (asprintf (&opt, "--seed-file=%s", seed_file) < 0)
	err = ENOMEM;
      else
	{
	  err = argz_add (argz, argz_len, opt);
	  free (opt);
	}
    }

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
  error_t err;
  struct port_info *inpi = ports_lookup_port (fsys->pi.bucket, handle,
					      shutdown_notify_class);

  if (!inpi)
    return EOPNOTSUPP;

  err = update_random_seed_file ();
  if (err)
    error (0, err, "Warning: Failed to save random seed to %s", seed_file);
  return 0;
}

void
sigterm_handler (int signo)
{
  error_t err;
  err = update_random_seed_file ();
  if (err)
    error (0, err, "Warning: Failed to save random seed to %s", seed_file);
  signal (SIGTERM, SIG_DFL);
  raise (SIGTERM);
}

static error_t
arrange_shutdown_notification ()
{
  error_t err;
  mach_port_t initport, notify;
  struct port_info *pi;

  shutdown_notify_class = ports_create_class (0, 0);

  if (signal (SIGTERM, sigterm_handler) == SIG_ERR)
    return errno;

  /* Arrange to get notified when the system goes down,
     but if we fail for some reason, just silently give up.  No big deal. */

  err = ports_create_port (shutdown_notify_class, fsys->pi.bucket,
			   sizeof (struct port_info), &pi);
  if (err)
    return err;

  initport = file_name_lookup (_SERVERS_STARTUP, 0, 0);
  if (! MACH_PORT_VALID (initport))
    return errno;

  notify = ports_get_send_right (pi);
  ports_port_deref (pi);
  err = startup_request_notification (initport, notify,
				      MACH_MSG_TYPE_MAKE_SEND,
				      program_invocation_short_name);

  mach_port_deallocate (mach_task_self (), notify);
  mach_port_deallocate (mach_task_self (), initport);
  return err;
}

int
main (int argc, char **argv)
{
  error_t err;
  unsigned int seed;
  mach_port_t bootstrap;

  /* We use the same argp for options available at startup
     as for options we'll accept in an fsys_set_options RPC.  */
  argp_parse (&random_argp, argc, argv, 0, 0, 0);

  pool_initialize ();

  err = read_random_seed_file ();
  if (err)
    error (0, err, "Warning: Failed to read random seed file %s", seed_file);

  /* Initialize the libcs PRNG.  */
  pool_randomize (&seed, sizeof seed);
  srandom (seed);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "trivfs_startup");

  err = arrange_shutdown_notification ();
  if (err)
    error (0, err, "Warning: Cannot request shutdown notification");

  err = start_gather_thread ();
  if (err)
    error (1, err, "Starting gather thread failed");

  /* Launch. */
  ports_manage_port_operations_multithread (fsys->pi.bucket, random_demuxer,
					    10 * 1000, /* idle thread */
					    10 * 60 * 1000, /* idle server */
					    0);
  return 0;
}
