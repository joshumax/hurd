/* A translator for doing I/O to mach kernel devices.

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

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/pager.h>
#include <hurd/trivfs.h>
#include <hurd/fsys.h>

#include <stdio.h>
#include <error.h>
#include <getopt.h>
#include <assert.h>
#include <fcntl.h>

#include "open.h"
#include "dev.h"

#ifdef MSG
#define DEBUG(what) \
  ((debug) \
   ? ({ mutex_lock(&debug_lock); what; mutex_unlock(&debug_lock);0;}) \
   : 0)
#else
#define DEBUG(what) 0
#endif

/* ---------------------------------------------------------------- */

/* The port class of our file system control pointer.  */
struct port_class *fsys_port_class;
/* The port class of the (only) root file port for the opened device.  */
struct port_class *root_port_class;

/* A bucket to put all our ports in.  */
struct port_bucket *port_bucket;

/* Trivfs noise.  */
struct port_class *trivfs_protid_portclasses[1];
struct port_class *trivfs_cntl_portclasses[1];
int trivfs_protid_nportclasses = 1;
int trivfs_cntl_nportclasses = 1;

/* ---------------------------------------------------------------- */

#define USAGE "Usage: %s [OPTION...] DEVICE\n"

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
  -d, --devnum=NUM           Give DEVICE a device number NUM\n\
  -r, --readonly             Disable writing to DEVICE\n\
  -p, --seekable             Enable seeking if DEVICE is serial\n\
  -s, --serial               Indicate that DEVICE has a single R/W point\n\
  -b, --buffered, --block    Open DEVICE in `block' mode, which allows reads\n\
                             or writes less than a single block and buffers\n\
                             I/O to the actual device.  By default, all reads\n\
                             and writes are made directly to the device,\n\
                             with no buffering, and any sub-block-size I/O\n\
                             is padded to the nearest full block.\n\
  -B NUM, --block-size=NUM   Use a block size of NUM, which must be an integer\n\
                             multiple of DEVICE's real block size\n\
");
    }

  exit(status);
}

#define SHORT_OPTIONS "bB:d:D:?rpsu"

static struct option options[] =
{
  {"block-size", required_argument, 0, 'B'},
  {"debug", required_argument,  0, 'D'},
  {"help", no_argument, 0, '?'},
  {"devnum", required_argument, 0, 'm'},
  {"block", no_argument, 0, 'b'},
  {"buffered", no_argument, 0, 'b'},
  {"readonly", no_argument, 0, 'r'},
  {"seekable", no_argument, 0, 'p'},
  {"serial", no_argument, 0, 's'},
  {0, 0, 0, 0}
};

/* ---------------------------------------------------------------- */

/* A struct dev for the open kernel device.  */
static struct dev *device = NULL;
/* And a lock to arbitrate changes to it.  */
static struct mutex device_lock;

/* Desired device parameters specified by the user.  */
static char *device_name = NULL;
static int device_flags = 0;
static int device_block_size = 0;

/* A unixy device number to return when the device is stat'd.  */
static int device_number = 0;

/* A stream on which we can print debugging message.  */
FILE  *debug = NULL;
/* A lock to use while doing so.  */
struct mutex debug_lock;

void main(int argc, char *argv[])
{
  int opt;
  error_t err;
  mach_port_t bootstrap;

  while ((opt = getopt_long(argc, argv, SHORT_OPTIONS, options, 0)) != EOF)
    switch (opt)
      {
      case 'r': device_flags |= DEV_READONLY; break;
      case 's': device_flags |= DEV_SERIAL; break;
      case 'b': device_flags |= DEV_BUFFERED; break;
      case 'p': device_flags |= DEV_SEEKABLE; break;
      case 'B': device_block_size = atoi(optarg); break;
      case 'd': device_number = atoi(optarg); break;
      case 'D': debug = fopen(optarg, "w+"); setlinebuf(debug); break;
      case '?': usage(0);
      default:  usage(1);
      }

  mutex_init(&debug_lock);

  if (device_flags & DEV_READONLY)
    /* Catch illegal writes at the point of open.  */
    trivfs_allow_open &= ~O_WRITE;

  if (argv[optind] == NULL || argv[optind + 1] != NULL)
    {
      fprintf(stderr, USAGE, program_invocation_name);
      usage(1);
    }

  device_name = argv[optind];

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error(2, 0, "Must be started as a translator");
  
  fsys_port_class = ports_create_class (trivfs_clean_cntl, 0);
  root_port_class = ports_create_class (trivfs_clean_protid, 0);
  port_bucket = ports_create_bucket ();
  trivfs_protid_portclasses[0] = root_port_class;
  trivfs_cntl_portclasses[0] = fsys_port_class;

  /* Reply to our parent */
  err =
    trivfs_startup(bootstrap,
		   fsys_port_class, port_bucket,
		   root_port_class, port_bucket,
		   NULL);
  if (err)
    error(3, err, "Contacting parent");

  /* Open the device only when necessary.  */
  device = NULL;
  mutex_init(&device_lock);

  /* Launch. */
  ports_manage_port_operations_multithread (port_bucket, trivfs_demuxer,
					    30*1000, 5*60*1000, 0, 0);

  exit(0);
}

/* Called whenever someone tries to open our node (even for a stat).  We
   delay opening the kernel device until this point, as we can usefully
   return errors from here.  */
static error_t
check_open_hook (struct trivfs_control *trivfs_control,
		 uid_t *uids, u_int nuids,
		 gid_t *gids, u_int ngids,
		 int flags)
{
  error_t err = 0;

  mutex_lock(&device_lock);
  if (device == NULL)
    /* Try and open the device.  */
    {
      err = dev_open(device_name, device_flags, device_block_size, &device);
      if (err)
	device = NULL;
      if (err && (flags & (O_READ|O_WRITE)) == 0)
	/* If we're not opening for read or write, then just ignore the
	   error, as this allows stat to word correctly.  XXX  */
	err = 0;
    }
  mutex_unlock(&device_lock);

  return err;
}

static void
open_hook(struct trivfs_peropen *peropen)
{
  struct dev *dev = device;
  if (dev)
    open_create(dev, (struct open **)&peropen->hook);
}

static void
close_hook(struct trivfs_peropen *peropen)
{
  if (peropen->hook)
    open_free(peropen->hook);
}

/* ---------------------------------------------------------------- */
/* Trivfs hooks  */

int trivfs_fstype = FSTYPE_DEV;
int trivfs_fsid = 0;

int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ | O_WRITE;

void
trivfs_modify_stat (struct stat *st)
{
  struct dev *dev = device;

  if (dev)
    {
      vm_size_t size = dev->size;

      if (dev->block_size > 1)
	st->st_blksize = dev->block_size;

      st->st_size = size;
      st->st_blocks = size / 512;

      if (dev_is(dev, DEV_READONLY))
	st->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);

      st->st_mode &= ~S_IFMT;
      st->st_mode |= dev_is(dev, DEV_BUFFERED) ? S_IFBLK : S_IFCHR;
    }
  else
    /* Try and do things without an open device...  */
    {
      st->st_blksize = device_block_size;
      st->st_size = 0;
      st->st_blocks = 0;
      st->st_mode &= ~S_IFMT;
      st->st_mode |= (device_flags & DEV_BUFFERED) ? S_IFBLK : S_IFCHR;
      if (device_flags & DEV_READONLY)
	st->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
    }

  st->st_rdev = device_number;
}

error_t
trivfs_goaway (int flags, mach_port_t realnode,
	       struct port_class *fsys_port_class,
	       struct port_class *file_port_class)
{
  int force = (flags & FSYS_GOAWAY_FORCE);
  int nosync = (flags & FSYS_GOAWAY_NOSYNC);

  DEBUG(fprintf(debug, "trivfs_goaway(0x%x, %d)\n", flags, realnode));

  mutex_lock(&device_lock);

  if (device == NULL)
    exit (0);

  /* Wait until all pending rpcs are done.  */
  ports_inhibit_class_rpcs (root_port_class);

  if (force && nosync)
    /* Exit with extreme prejudice.  */
    exit (0);

  if (!force && ports_count_class (root_port_class) > 0)
    /* Still users, so don't exit.  */
    goto busy;

  if (!nosync)
    /* Sync the device here, if necessary, so that closing it won't result in
       any I/O (which could get hung up trying to use one of our pagers).  */
    dev_sync (device, 1);

  /* devpager_shutdown may sync the pagers as side-effect (if NOSYNC is 0),
     so we put that first in this test.  */
  if (dev_stop_paging (device, nosync) || force)
    /* Bye-bye.  */
    {
      if (!nosync)
	/* If NOSYNC is true, we don't close DEV, as that could cause data to
	   be written back.  */
	dev_close (device);
      exit (0);
    }

 busy:
  /* Allow normal operations to proceed.  */
  ports_enable_class (root_port_class);
  mutex_unlock(&device_lock);

  /* Complain that there are still users.  */
  return EBUSY;
}

/* If this variable is set, it is called every time an open happens.
   UIDS, GIDS, and FLAGS are from the open; CNTL identifies the
   node being opened.  This call need not check permissions on the underlying
   node.  If the open call should block, then return EWOULDBLOCK.  Other
   errors are immediately reflected to the user.  If O_NONBLOCK 
   is not set in FLAGS and EWOULDBLOCK is returned, then call 
   trivfs_complete_open when all pending open requests for this 
   file can complete. */
error_t (*trivfs_check_open_hook)(struct trivfs_control *trivfs_control,
				  uid_t *uids, u_int nuids,
				  gid_t *gids, u_int ngids,
				  int flags)
     = check_open_hook;

/* If this variable is set, it is called every time a new peropen
   structure is created and initialized. */
void (*trivfs_peropen_create_hook)(struct trivfs_peropen *) = open_hook;

/* If this variable is set, it is called every time a peropen structure
   is about to be destroyed. */
void (*trivfs_peropen_destroy_hook) (struct trivfs_peropen *) = close_hook;

/* Sync this filesystem.  */
kern_return_t
trivfs_S_fsys_syncfs (struct trivfs_control *cntl,
		      mach_port_t reply, mach_msg_type_name_t replytype,
		      int wait, int dochildren)
{
  struct dev *dev = device;

  DEBUG(fprintf(debug, "Syncing filesystem...\n"));

  if (dev)
    return dev_sync(dev, wait);
  else
    return 0;
}

void
thread_cancel (thread_t foo __attribute__ ((unused)))
{
}
