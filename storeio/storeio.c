/* A translator for doing I/O to stores

   Copyright (C) 1995,96,97,98,99,2000,01,02 Free Software Foundation, Inc.
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
#include <error.h>
#include <assert-backtrace.h>
#include <fcntl.h>
#include <argp.h>
#include <argz.h>
#include <sys/sysmacros.h>

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <version.h>

#include "open.h"
#include "dev.h"
#include "libtrivfs/trivfs_fsys_S.h"

static struct argp_option options[] =
{
  {"readonly", 'r', 0,	  0,"Disallow writing"},
  {"writable", 'w', 0,	  0,"Allow writing"},
  {"no-cache", 'c', 0,	  0,"Never cache data--user io does direct device io"},
  {"no-file-io", 'F', 0,  0,"Never perform io via plain file io RPCs"},
  {"no-fileio",  0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"enforced",  'e', 0,	  0,"Never reveal underlying devices, even to root"},
  {"rdev",     'n', "ID", 0,
   "The stat rdev number for this node; may be either a"
   " single integer, or of the form MAJOR,MINOR"},
  {0}
};
static const char doc[] = "Translator for devices and other stores";

const char *argp_program_version = STANDARD_HURD_VERSION (storeio);

/* Desired store parameters specified by the user.  */
struct storeio_argp_params
{
  struct store_argp_params store_params; /* Filled in by store_argp parser.  */
  struct dev *dev;		/* We fill in its flag members.  */
};

/* Parse a single option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct storeio_argp_params *params = state->input;

  switch (key)
    {

    case 'r': params->dev->readonly = 1; break;
    case 'w': params->dev->readonly = 0; break;

    case 'c': params->dev->inhibit_cache = 1; break;
    case 'e': params->dev->enforced = 1; break;
    case 'F': params->dev->no_fileio = 1; break;

    case 'n':
      {
	char *start = arg, *end;
	dev_t rdev;

	rdev = strtoul (start, &end, 0);
	if (*end == ',')
	  /* MAJOR,MINOR form */
	  {
	    start = end + 1;
	    rdev = gnu_dev_makedev (rdev, strtoul (start, &end, 0));
	  }

	if (end == start || *end != '\0')
	  {
	    argp_error (state, "%s: Invalid argument to --rdev", arg);
	    return EINVAL;
	  }

	params->dev->rdev = rdev;
      }
      break;

    case ARGP_KEY_INIT:
      /* Now store_argp's parser will get to initialize its state.
	 The default_type member is our input parameter to it.  */
      memset (&params->store_params, 0, sizeof params->store_params);
      params->store_params.default_type = "device";
      params->store_params.store_optional = 1;
      state->child_inputs[0] = &params->store_params;
      break;

    case ARGP_KEY_SUCCESS:
      params->dev->store_name = params->store_params.result;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp_child argp_kids[] = { { &store_argp }, {0} };
static const struct argp argp = { options, parse_opt, 0, doc, argp_kids };

struct trivfs_control *storeio_fsys;

int
main (int argc, char *argv[])
{
  error_t err;
  mach_port_t bootstrap;
  struct dev device;
  struct storeio_argp_params params;

  memset (&device, 0, sizeof device);
  pthread_mutex_init (&device.lock, NULL);

  params.dev = &device;
  argp_parse (&argp, argc, argv, 0, 0, &params);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (2, 0, "Must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &storeio_fsys);
  if (err)
    error (3, err, "trivfs_startup");

  storeio_fsys->hook = &device;

  /* Launch. */
  ports_manage_port_operations_multithread (storeio_fsys->pi.bucket,
					    trivfs_demuxer,
					    30*1000, 5*60*1000, 0);

  return 0;
}

error_t
trivfs_append_args (struct trivfs_control *trivfs_control,
		    char **argz, size_t *argz_len)
{
  struct dev *const dev = trivfs_control->hook;
  error_t err = 0;

  if (dev->rdev != (dev_t) 0)
    {
      char buf[40];
      snprintf (buf, sizeof buf, "--rdev=%d,%d",
		gnu_dev_major (dev->rdev), gnu_dev_minor (dev->rdev));
      err = argz_add (argz, argz_len, buf);
    }

  if (!err && dev->inhibit_cache)
    err = argz_add (argz, argz_len, "--no-cache");

  if (!err && dev->enforced)
    err = argz_add (argz, argz_len, "--enforced");

  if (!err && dev->no_fileio)
    err = argz_add (argz, argz_len, "--no-file-io");

  if (! err)
    err = argz_add (argz, argz_len,
		    dev->readonly ? "--readonly" : "--writable");

  if (! err)
    err = store_parsed_append_args (dev->store_name, argz, argz_len);

  return err;
}

/* Called whenever a new lookup is done of our node.  The only reason we
   set this hook is to duplicate the check done normally done against
   trivfs_allow_open in trivfs_S_fsys_getroot, but looking at the
   per-device state.  This gets checked again in check_open_hook, but this
   hook runs before a little but more overhead gets incurred.  In the
   success case, we just return EAGAIN to have trivfs_S_fsys_getroot
   continue with its generic processing.  */
static error_t
getroot_hook (struct trivfs_control *cntl,
	      mach_port_t reply_port,
	      mach_msg_type_name_t reply_port_type,
	      mach_port_t dotdot,
	      uid_t *uids, u_int nuids, uid_t *gids, u_int ngids,
	      int flags,
	      retry_type *do_retry, char *retry_name,
	      mach_port_t *node, mach_msg_type_name_t *node_type)
{
  struct dev *const dev = cntl->hook;
  return (dev_is_readonly (dev) && (flags & O_WRITE)) ? EROFS : EAGAIN;
}

/* Called whenever someone tries to open our node (even for a stat).  We
   delay opening the kernel device until this point, as we can usefully
   return errors from here.  */
static error_t
check_open_hook (struct trivfs_control *trivfs_control,
		 struct iouser *user,
		 int flags)
{
  struct dev *const dev = trivfs_control->hook;
  error_t err = 0;

  if (!err && dev_is_readonly (dev) && (flags & O_WRITE))
    return EROFS;

  pthread_mutex_lock (&dev->lock);
  if (dev->store == NULL)
    {
      /* Try and open the store.  */
      err = dev_open (dev);
      if (err && (flags & (O_READ|O_WRITE)) == 0)
	/* If we're not opening for read or write, then just ignore the
	   error, as this allows stat to work correctly.  XXX  */
	err = 0;
    }
  pthread_mutex_unlock (&dev->lock);

  return err;
}

static error_t
open_hook (struct trivfs_peropen *peropen)
{
  error_t err = 0;
  struct dev *const dev = peropen->cntl->hook;

  if (dev->store)
    {
      pthread_mutex_lock (&dev->lock);
      if (dev->nperopens++ == 0)
	err = store_clear_flags (dev->store, STORE_INACTIVE);
      pthread_mutex_unlock (&dev->lock);
      if (!err)
	err = open_create (dev, (struct open **)&peropen->hook);
    }
  return err;
}

static void
close_hook (struct trivfs_peropen *peropen)
{
  struct dev *const dev = peropen->cntl->hook;

  if (peropen->hook)
    {
      pthread_mutex_lock (&dev->lock);
      if (--dev->nperopens == 0)
	store_set_flags (dev->store, STORE_INACTIVE);
      pthread_mutex_unlock (&dev->lock);
      open_free (peropen->hook);
    }
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
  struct dev *const dev = cred->po->cntl->hook;
  struct open *open = cred->po->hook;

  st->st_mode &= ~S_IFMT;

  if (open)
    /* An open device.  */
    {
      struct store *store = open->dev->store;
      store_offset_t size = store->size;

      if (store->block_size > 1)
	st->st_blksize = store->block_size;

      st->st_size = size;
      st->st_mode |= ((dev->inhibit_cache || store->block_size == 1)
		      ? S_IFCHR : S_IFBLK);
    }
  else
    /* Try and do things without an open device...  */
    {
      st->st_blksize = 0;
      st->st_size = 0;

      st->st_mode |= dev->inhibit_cache ? S_IFCHR : S_IFBLK;
    }

  st->st_rdev = dev->rdev;
  if (dev_is_readonly (dev))
    st->st_mode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  struct dev *const device = fsys->hook;
  error_t err;
  int force = (flags & FSYS_GOAWAY_FORCE);
  int nosync = (flags & FSYS_GOAWAY_NOSYNC);
  struct port_class *root_port_class = fsys->protid_class;

  pthread_mutex_lock (&device->lock);

  if (device->store == NULL)
    /* The device is not actually open.
       XXX note that exitting here nukes non-io users, like someone
       in the middle of a stat who will get SIGLOST or something.  */
    exit (0);

  /* Wait until all pending rpcs are done.  */
  err = ports_inhibit_class_rpcs (root_port_class);
  if (err == EINTR || (err && !force))
    {
      pthread_mutex_unlock (&device->lock);
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
  pthread_mutex_unlock (&device->lock);

  /* Complain that there are still users.  */
  return EBUSY;
}

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
				mach_port_t *node, mach_msg_type_name_t *node_type)
     = getroot_hook;

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
  struct dev *dev = cntl->hook;
  if (dev)
    return dev_sync (dev, wait);
  else
    return 0;
}
