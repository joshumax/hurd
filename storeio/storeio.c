/* A translator for doing I/O to stores

   Copyright (C) 1995, 96, 97, 98, 99 Free Software Foundation, Inc.

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
#include <error.h>
#include <assert.h>
#include <fcntl.h>
#include <argp.h>
#include <argz.h>

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <version.h>

#include "open.h"
#include "dev.h"

static struct argp_option options[] =
{
  {"readonly", 'r', 0,	  0,"Disallow writing"},
  {"writable", 'w', 0,	  0,"Allow writing"},
  {"no-cache", 'c', 0,	  0,"Never cache data--user io does direct device io"},
  {"rdev",     'n', "ID", 0,
   "The stat rdev number for this node; may be either a"
   " single integer, or of the form MAJOR,MINOR"},
  {0}
};
static const char doc[] = "Translator for devices and other stores";

const char *argp_program_version = STANDARD_HURD_VERSION (storeio);

/* The open store.  */
static struct dev *device = NULL;
/* And a lock to arbitrate changes to it.  */
static struct mutex device_lock;

/* Desired store parameters specified by the user.  */
struct store_parsed *store_name;
static int readonly;

/* Nonzero if user gave --no-cache flag.  */
static int inhibit_cache;

/* A unixy device number to return when the device is stat'd.  */
static int rdev;

/* Parse a single option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'r': readonly = 1; break;
    case 'w': readonly = 0; break;

    case 'c': inhibit_cache = 1; break;

    case 'n':
      {
	char *start = arg, *end;

	rdev = strtoul (start, &end, 0);
	if (*end == ',')
	  /* MAJOR,MINOR form */
	  {
	    start = end;
	    rdev = (rdev << 8) + strtoul (start, &end, 0);
	  }

	if (end == start || *end != '\0')
	  {
	    argp_error (state, "%s: Invalid argument to --rdev", arg);
	    return EINVAL;
	  }
      }
      break;

    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input; break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp_child argp_kids[] = { { &store_argp }, {0} };
static const struct argp argp = { options, parse_opt, 0, doc, argp_kids };

int
main (int argc, char *argv[])
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;
  struct store_argp_params store_params = { default_type: "device" };

  argp_parse (&argp, argc, argv, 0, 0, &store_params);
  store_name = store_params.result;

  if (readonly)
    /* Catch illegal writes at the point of open.  */
    trivfs_allow_open &= ~O_WRITE;

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (2, 0, "Must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &fsys);
  if (err)
    error (3, err, "trivfs_startup");

  /* Open the device only when necessary.  */
  device = NULL;
  mutex_init (&device_lock);

  /* Launch. */
  ports_manage_port_operations_multithread (fsys->pi.bucket, trivfs_demuxer,
					    30*1000, 5*60*1000, 0);

  return 0;
}

error_t
trivfs_append_args (struct trivfs_control *trivfs_control,
		    char **argz, size_t *argz_len)
{
  error_t err = 0;

  if (rdev)
    {
      char buf[40];
      snprintf (buf, sizeof buf, "--rdev=%d,%d", (rdev >> 8), rdev & 0xFF);
      err = argz_add (argz, argz_len, buf);
    }

  if (!err && inhibit_cache)
    err = argz_add (argz, argz_len, "--no-cache");

  if (! err)
    err = argz_add (argz, argz_len, readonly ? "--readonly" : "--writable");

  if (! err)
    err = store_parsed_append_args (store_name, argz, argz_len);

  return err;
}

/* Called whenever someone tries to open our node (even for a stat).  We
   delay opening the kernel device until this point, as we can usefully
   return errors from here.  */
static error_t
check_open_hook (struct trivfs_control *trivfs_control,
		 struct iouser *user,
		 int flags)
{
  error_t err = 0;

  if (!err && readonly && (flags & O_WRITE))
    return EROFS;

  mutex_lock (&device_lock);
  if (device == NULL)
    /* Try and open the device.  */
    {
      err = dev_open (store_name, readonly ? STORE_READONLY : 0, inhibit_cache,
		      &device);
      if (err)
	device = NULL;
      if (err && (flags & (O_READ|O_WRITE)) == 0)
	/* If we're not opening for read or write, then just ignore the
	   error, as this allows stat to word correctly.  XXX  */
	err = 0;
    }
  mutex_unlock (&device_lock);

  return err;
}

static error_t
open_hook (struct trivfs_peropen *peropen)
{
  struct dev *dev = device;
  if (dev)
    return open_create (dev, (struct open **)&peropen->hook);
  else
    return 0;
}

static void
close_hook (struct trivfs_peropen *peropen)
{
  if (peropen->hook)
    open_free (peropen->hook);
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
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  struct open *open = cred->po->hook;

  st->st_mode &= ~S_IFMT;

  if (open)
    /* An open device.  */
    {
      struct store *store = open->dev->store;
      vm_size_t size = store->size;

      if (store->block_size > 1)
	st->st_blksize = store->block_size;

      st->st_size = size;
      st->st_blocks = size / 512;
      
      st->st_mode |= ((inhibit_cache || store->block_size == 1)
		      ? S_IFCHR : S_IFBLK);
    }
  else
    /* Try and do things without an open device...  */
    {
      st->st_blksize = 0;
      st->st_size = 0;
      st->st_blocks = 0;
      
      st->st_mode |= inhibit_cache ? S_IFCHR : S_IFBLK;
    }

  st->st_rdev = rdev;
  if (readonly)
    st->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  error_t err;
  int force = (flags & FSYS_GOAWAY_FORCE);
  int nosync = (flags & FSYS_GOAWAY_NOSYNC);
  struct port_class *root_port_class = fsys->protid_class;

  mutex_lock (&device_lock);

  if (device == NULL)
    exit (0);

  /* Wait until all pending rpcs are done.  */
  err = ports_inhibit_class_rpcs (root_port_class);
  if (err == EINTR || (err && !force))
    {
      mutex_unlock (&device_lock);
      return err;
    }

  if (force && nosync)
    /* Exit with extreme prejudice.  */
    exit (0);

  if (!force && ports_count_class (root_port_class) > 0)
    /* Still users, so don't exit.  */
    goto busy;

  if (! nosync)
    /* Sync the device here, if necessary, so that closing it won't result in
       any I/O (which could get hung up trying to use one of our pagers).  */
    dev_sync (device, 1);

  /* devpager_shutdown may sync the pagers as side-effect (if NOSYNC is 0),
     so we put that first in this test.  */
  if (dev_stop_paging (device, nosync) || force)
    /* Bye-bye.  */
    {
      if (! nosync)
	/* If NOSYNC is true, we don't close DEV, as that could cause data to
	   be written back.  */
	dev_close (device);
      exit (0);
    }

 busy:
  /* Allow normal operations to proceed.  */
  ports_enable_class (root_port_class);
  ports_resume_class_rpcs (root_port_class);
  mutex_unlock (&device_lock);

  /* Complain that there are still users.  */
  return EBUSY;
}

/* If this variable is set, it is called every time an open happens.
   USER and FLAGS are from the open; CNTL identifies the
   node being opened.  This call need not check permissions on the underlying
   node.  If the open call should block, then return EWOULDBLOCK.  Other
   errors are immediately reflected to the user.  If O_NONBLOCK
   is not set in FLAGS and EWOULDBLOCK is returned, then call
   trivfs_complete_open when all pending open requests for this
   file can complete. */
error_t (*trivfs_check_open_hook)(struct trivfs_control *trivfs_control,
				  struct iouser *user,
				  int flags)
     = check_open_hook;

/* If this variable is set, it is called every time a new peropen
   structure is created and initialized. */
error_t (*trivfs_peropen_create_hook)(struct trivfs_peropen *) = open_hook;

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
  if (dev)
    return dev_sync (dev, wait);
  else
    return 0;
}
