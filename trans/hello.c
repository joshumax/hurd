/* hello.c - A trivial single-file translator
   Copyright (C) 1998,1999,2001,02,2006 Free Software Foundation, Inc.
   Gordon Matzigkeit <gord@fig.org>, 1999
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
#include <stdio.h>
#include <stdlib.h>
#include <argp.h>
#include <argz.h>
#include <error.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <version.h>

#include "libtrivfs/trivfs_io_S.h"

const char *argp_program_version = STANDARD_HURD_VERSION (hello);

/* The message we return when we are read.  */
static const char hello[] = "Hello, world!\n";
static char *contents = (char *) hello;
static size_t contents_len = sizeof hello - 1;

/* Trivfs hooks. */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;

int trivfs_allow_open = O_READ;

int trivfs_support_read = 1;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;

/* NOTE: This example is not robust: it is possible to trigger some
   assertion failures because we don't implement the following:

   $ cd /src/hurd/libtrivfs
   $ grep -l 'assert.*!trivfs_support_read' *.c |
     xargs grep '^trivfs_S_' | sed 's/^[^:]*:\([^ 	]*\).*$/\1/'
   trivfs_S_io_get_openmodes
   trivfs_S_io_clear_some_openmodes
   trivfs_S_io_set_some_openmodes
   trivfs_S_io_set_all_openmodes
   trivfs_S_io_readable
   trivfs_S_io_select
   $

   For that reason, you should run this as an active translator
   `settrans -ac testnode /path/to/thello' so that you can see the
   error messages when they appear. */

/* A hook for us to keep track of the file descriptor state. */
struct open
{
  off_t offs;
};

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  /* Mark the node as a read-only plain file. */
  st->st_mode &= ~(S_IFMT | ALLPERMS);
  st->st_mode |= (S_IFREG | S_IRUSR | S_IRGRP | S_IROTH);
  st->st_size = contents_len;
}

error_t
trivfs_goaway (struct trivfs_control *cntl, int flags)
{
  exit (0);
}


static error_t
open_hook (struct trivfs_peropen *peropen)
{
  struct open *op = malloc (sizeof (struct open));
  if (op == NULL)
    return ENOMEM;

  /* Initialize the offset. */
  op->offs = 0;
  peropen->hook = op;
  return 0;
}


static void
close_hook (struct trivfs_peropen *peropen)
{
  free (peropen->hook);
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
  struct open *op;

  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_READ))
    return EBADF;

  /* Get the offset. */
  op = cred->po->hook;
  if (offs == -1)
    offs = op->offs;

  /* Prune the amount they want to read. */
  if (offs > contents_len)
    offs = contents_len;
  if (offs + amount > contents_len)
    amount = contents_len - offs;

  if (amount > 0)
    {
      /* Possibly allocate a new buffer. */
      if (*data_len < amount)
	{
	  *data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  if (*data == MAP_FAILED)
	    return ENOMEM;
	}

      /* Copy the constant data into the buffer. */
      memcpy ((char *) *data, contents + offs, amount);

      /* Update the saved offset. */
      op->offs += amount;
    }

  *data_len = amount;
  return 0;
}


/* Change current read/write offset */
error_t
trivfs_S_io_seek (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  off_t offs, int whence, off_t *new_offs)
{
  struct open *op;
  error_t err = 0;
  if (! cred)
    return EOPNOTSUPP;

  op = cred->po->hook;
  switch (whence)
    {
    case SEEK_CUR:
      offs += op->offs;
      goto check;
    case SEEK_END:
      offs += contents_len;
    case SEEK_SET:
    check:
      if (offs >= 0)
	{
	  *new_offs = op->offs = offs;
	  break;
	}
    default:
      err = EINVAL;
    }

  return err;
}


/* If this variable is set, it is called every time a new peropen
   structure is created and initialized. */
error_t (*trivfs_peropen_create_hook)(struct trivfs_peropen *) = open_hook;

/* If this variable is set, it is called every time a peropen structure
   is about to be destroyed. */
void (*trivfs_peropen_destroy_hook) (struct trivfs_peropen *) = close_hook;


/* Options processing.  We accept the same options on the command line
   and from fsys_set_options.  */

static const struct argp_option options[] =
{
  {"contents",	'c', "STRING",	0, "Specify the contents of the virtual file"},
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

    case 'c':
      {
	char *new = strdup (arg);
	if (new == NULL)
	  return ENOMEM;
	if (contents != hello)
	  free (contents);
	contents = new;
	contents_len = strlen (new);
	break;
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
  error_t err;
  char *opt;
  size_t opt_len;
  FILE *s;
  char *c;

  s = open_memstream (&opt, &opt_len);
  fprintf (s, "--contents='");

  for (c = contents; *c; c++)
    switch (*c)
      {
      case 0x27: /* Single quote.  */
	fprintf (s, "'\"'\"'");
	break;

      default:
	fprintf (s, "%c", *c);
      }

  fprintf (s, "'");
  fclose (s);

  err = argz_add (argz, argz_len, opt);

  free (opt);

  return err;
}

static struct argp hello_argp =
{ options, parse_opt, 0, "A translator providing a warm greeting." };

/* Setting this variable makes libtrivfs use our argp to
   parse options passed in an fsys_set_options RPC.  */
struct argp *trivfs_runtime_argp = &hello_argp;


int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;

  /* We use the same argp for options available at startup
     as for options we'll accept in an fsys_set_options RPC.  */
  argp_parse (&hello_argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "trivfs_startup");

  /* Launch. */
  ports_manage_port_operations_one_thread (fsys->pi.bucket, trivfs_demuxer, 0);

  return 0;
}
