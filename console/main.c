/* main.c - The main routine of the console server.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <hurd.h>
#include <fcntl.h>
#include <hurd/trivfs.h>
#include <hurd/fsys.h>
#include <stdio.h>
#include <argp.h>
#include <error.h>
#include <string.h>

#include <version.h>

#include "priv.h"
#include "tioctl_S.h"
#include "console.h"
#include "focus.h"


const char *argp_program_version = STANDARD_HURD_VERSION (console);

int trivfs_fstype = FSTYPE_DEV;
int trivfs_fsid = 0;
int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ|O_WRITE;

/* Properties of the underlying node.  */
mode_t console_mode;
uid_t console_owner;
gid_t console_group;

/* The argument line options.  */
struct
{
  char *encoding;
  int rdev;
} main_config;

static struct argp_option options[] =
  {
    {"rdev",     'n', "ID", 0,
     "The stat rdev number for this node; may be either a"
     " single integer, or of the form MAJOR,MINOR"},
    {"encoding", 'e', "NAME", 0,
     "The encoding to use for input and output"},
    {0}
  };

static struct argp_child argp_childs[] =
  {
    FOCUS_ARGP_CHILD,
    /* XXX CONSOLE_ARGP_CHILD, */
    { 0 }
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
    case 'n':
      {
        char *start = arg;
        char *end;
	int rdev;

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
	main_config.rdev = rdev;
      }
      break;
    }
  return 0;
}

static struct argp main_argp =
  { options, parse_opt, 0, "A translator providing a console device.\v"\
    "The translator provides access to a hardeware console usable\n"\
    "by the HURDIO backend of the term translator.\n", argp_childs };


int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;
  struct stat st;

  argp_parse (&main_argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  /* Set our node */
  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (1, err, "Starting translator");

  /* Initialize status from underlying node.  */
  err = io_stat (fsys->underlying, &st);
  if (err)
    {
      /* We cannot stat the underlying node.  Fallback to the defaults.  */
      console_owner = console_group = 0;
      console_mode = S_IRUSR | S_IWUSR;
      err = 0;
    }
  else
    {
      console_owner = st.st_uid;
      console_group = st.st_gid;
      console_mode = (st.st_mode & ACCESSPERMS);
    }
  console_mode |= S_IFCHR | S_IROOT;

  /* Launch.  */
  ports_manage_port_operations_multithread (fsys->pi.bucket, trivfs_demuxer,
					    0, 0, 0);

  return 0;
}

kern_return_t
S_tioctl_tiocflush (struct trivfs_protid *cred, int queue_selector)
{
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;
  if (!(cred->po->openmodes & (O_READ | O_WRITE)))
    return EBADF;

  vcons = (vcons_t) cred->po->hook;
  if (!queue_selector)
    queue_selector = O_READ | O_WRITE;

  if (queue_selector & O_READ)
    vcons_flush_input (vcons);
   if (queue_selector & O_WRITE)
    vcons_discard_output (vcons);

  return 0;
}


kern_return_t
S_tioctl_tiogwinsz (struct trivfs_protid *cred, struct winsize *size)
{
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;

  vcons = (vcons_t) cred->po->hook;
  vcons_getsize (vcons, size);
  return 0;
}


kern_return_t
S_tioctl_tiocstart (struct trivfs_protid *cred)
{
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;
  if (!(cred->po->openmodes & (O_READ | O_WRITE)))
    return EBADF;

  vcons = (vcons_t) cred->po->hook;
  vcons_start_output (vcons);
  return 0;
}


kern_return_t
S_tioctl_tiocstop (struct trivfs_protid *cred)
{
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;
  if (!(cred->po->openmodes & (O_READ | O_WRITE)))
    return EBADF;

  vcons = (vcons_t) cred->po->hook;
  vcons_stop_output (vcons);
  return 0;
}


kern_return_t
S_tioctl_tiocoutq (struct trivfs_protid *cred, int *queue_size)
{
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;
  if (!(cred->po->openmodes & (O_READ | O_WRITE)))
    return EBADF;

  vcons = (vcons_t) cred->po->hook;
  *queue_size = vcons_pending_output (vcons);
  return 0;
}


kern_return_t
S_tioctl_tiocspgrp (struct trivfs_protid *cred, int pgrp)
{
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;
  if (!(cred->po->openmodes & (O_READ | O_WRITE)))
    return EBADF;

  vcons = (vcons_t) cred->po->hook;
  vcons_set_owner (vcons, -pgrp);
  return 0;
}


kern_return_t
S_tioctl_tiocgpgrp (struct trivfs_protid *cred, int *pgrp)
{
  error_t err;
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;
  if (!(cred->po->openmodes & (O_READ | O_WRITE)))
    return EBADF;

  vcons = (vcons_t) cred->po->hook;
  err = vcons_get_owner (vcons, pgrp);
  if (!err)
    *pgrp = -*pgrp;

  return err;
}


error_t
trivfs_S_file_set_size (struct trivfs_protid *cred, off_t size)
{
  if (!cred)
    return EOPNOTSUPP;
  if (!(cred->po->openmodes & (O_READ | O_WRITE)))
    return EBADF;
  return 0;
}


error_t
trivfs_S_io_seek (struct trivfs_protid *cred, off_t off, int whence,
		  off_t *newp)
{
  return ESPIPE;
}


void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_blksize = 512;
  st->st_ino = 0;
  st->st_rdev = main_config.rdev;
  st->st_mode = console_mode;
  st->st_uid = console_owner;
  st->st_gid = console_group;
}


/* Called for user writes to the console as described in
   <hurd/io.defs>.  */
error_t
trivfs_S_io_write (struct trivfs_protid *cred, char *data, u_int datalen,
		   off_t offset, int *amount)
{
  error_t err = 0;
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;
  if (! (cred->po->openmodes & O_WRITE))
    return EBADF;

  vcons = (vcons_t) cred->po->hook;
  *amount = vcons_output (vcons, cred->po->openmodes & O_NONBLOCK,
			  data, datalen);
  if (*amount == -1)
    err = errno;

  return err;
}


/* Called for user reads from the console.  */
error_t
trivfs_S_io_read (struct trivfs_protid *cred, char **data, u_int *datalen,
		  off_t offset, int amount)
{
  if (!cred)
    return EOPNOTSUPP;
  if (! (cred->po->openmodes & O_READ))
    return EBADF;

  /* XXX */
  return EOPNOTSUPP;
}


error_t
trivfs_S_io_select (struct trivfs_protid *cred, int *type)
{
  error_t err = 0;
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;

  if (! (cred->po->openmodes & O_READ))
    *type &= ~SELECT_READ;
  if (! (cred->po->openmodes & O_WRITE))
    *type &= ~SELECT_WRITE;

  vcons = (vcons_t) cred->po->hook;
  if (type)
    err = vcons_select (vcons, type);
  return err;
}


error_t
trivfs_S_io_readable (struct trivfs_protid *cred, int *amt)
{
  if (!cred)
    return EOPNOTSUPP;
  if ((cred->po->openmodes & O_READ) == 0)
    return EBADF;

  /* XXX */
  //  *amt = qsize (inputq);
  return 0;
}


kern_return_t
trivfs_S_io_get_openmodes (struct trivfs_protid *cred, int *bits)
{
  return EOPNOTSUPP;
}


error_t
trivfs_S_io_set_all_openmodes (struct trivfs_protid *cred, int bits)
{
  return EOPNOTSUPP;
}


error_t
trivfs_S_io_set_some_openmodes (struct trivfs_protid *cred, int bits)
{
  return EOPNOTSUPP;
}


error_t
trivfs_S_io_clear_some_openmodes (struct trivfs_protid *cred, int bits)
{
  return EOPNOTSUPP;
}


error_t
trivfs_S_io_mod_owner (struct trivfs_protid *cred, pid_t owner)
{
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;
  if (!(cred->po->openmodes & (O_READ | O_WRITE)))
    return EBADF;

  vcons = (vcons_t) cred->po->hook;
  vcons_set_owner (vcons, owner);
  return 0;
}


error_t
trivfs_S_io_get_owner (struct trivfs_protid *cred, pid_t *owner)
{
  error_t err;
  vcons_t vcons;

  if (!cred)
    return EOPNOTSUPP;
  if (!(cred->po->openmodes & (O_READ | O_WRITE)))
    return EBADF;

  vcons = (vcons_t) cred->po->hook;
  err = vcons_get_owner (vcons, owner);
  return err;
}


error_t
trivfs_goaway (struct trivfs_control *cntl, int flags)
{
  return EBUSY;
}
