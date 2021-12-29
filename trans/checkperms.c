/* checkperms.c - A permission-checking and granting translator
   Copyright (C) 1998,99,2001,02,2006 Free Software Foundation, Inc.

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
#include <idvec.h>
#include <stdio.h>
#include <stdlib.h>
#include <argp.h>
#include <argz.h>
#include <error.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <portinfo.h>
#include <grp.h>
#include <ps.h>
#include <portxlate.h>

#include <version.h>

#include "libtrivfs/trivfs_io_S.h"

const char *argp_program_version = STANDARD_HURD_VERSION (checkperms);

/* The message we return when we are read.  */
static const char hello[] = "Hello, perms!\n";
static char *contents = (char *) hello;
static size_t contents_len = sizeof hello - 1;
static const char defaultgroupname[] = "audio";
static char *groupname = (char *) defaultgroupname;

/* This lock protects access to contents and contents_len.  */
static pthread_rwlock_t contents_lock;

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
  pthread_mutex_t lock;
  off_t offs;
};


/* adapted from utils/portinfo.c::search_for_port: Locates the port
   NAME from TASK in any other process and prints the mappings. */
error_t
search_for_pid (char **output, task_t task, mach_port_t name)
{
  error_t err;

  /* These resources are freed in the function epilogue.  */
  struct ps_context *context = NULL;
  struct proc_stat_list *procset = NULL;

  static process_t proc = MACH_PORT_NULL;
  if (proc == MACH_PORT_NULL)
    proc = getproc ();

  pid_t pid;
  err = proc_task2pid (proc, task, &pid);
  if (err)
    goto out;

  /* Get a list of all processes.  */
  err = ps_context_create (getproc (), &context);
  if (err)
    goto out;

  err = proc_stat_list_create (context, &procset);
  if (err)
    goto out;

  err = proc_stat_list_add_all (procset, 0, 0);
  if (err)
    goto out;

  for (unsigned i = 0; i < procset->num_procs; i++)
    {
      /* Ignore the target process.  */
      if (procset->proc_stats[i]->pid == pid)
	continue;

      task_t xlate_task = MACH_PORT_NULL;
      err = proc_pid2task (proc, procset->proc_stats[i]->pid, &xlate_task);
      if (err || xlate_task == MACH_PORT_NULL)
	continue;

      struct port_name_xlator *xlator = NULL;
      err = port_name_xlator_create (task, xlate_task, &xlator);
      if (err)
	goto loop_cleanup;

      mach_port_t translated_port;
      mach_msg_type_name_t translated_type;
        err = port_name_xlator_xlate (xlator,
				    name, 0,
				    &translated_port, &translated_type);
      if (err)
	goto loop_cleanup;

      /* The port translation was successful, print more infos.  */
      // output again, not called??
      asprintf (output, "% 5i", procset->proc_stats[i]->pid);
      goto out;

    loop_cleanup:
      if (xlate_task)
	mach_port_deallocate (mach_task_self (), xlate_task);

      if (xlator)
	port_name_xlator_free (xlator);
    }

  err = 0;

 out:
  if (procset != NULL)
    proc_stat_list_free (procset);

  if (context != NULL)
    ps_context_free (context);

  return err;
}


int check_group (struct idvec *gids) {
    /* Check whether the process has the checked group */
  unsigned has_group = 0;
  struct group _gr, *gr;
  char buf[1024];
  for (unsigned i = 0; i < gids->num; i++) {
    if (getgrgid_r (gids->ids[i], &_gr, buf, sizeof buf, &gr) == 0 && gr)
      {
        if (strcmp(groupname, strdup (gr->gr_name)) == 0)
          {
            has_group += 1;
          }
      }
  }
  return has_group;
}


error_t request_auth (struct trivfs_protid *cred) {
      /* specify the contents to show dynamically */
  struct port_info info = cred->pi;
  // struct rpc_info *rpcs = info.current_rpcs;
  struct port_bucket *bucket = info.bucket;
  mach_port_t portright = info.port_right;
  
  task_t task = mach_task_self ();
  unsigned show = 0;
  show |= PORTINFO_DETAILS;
  const char otherinfo_arr[1024];
  char *otherinfo = (char *) otherinfo_arr;
  search_for_pid (&otherinfo, task, portright);
  // for debugging:
  // contents_len = asprintf(&dat, "%d\n%d\n%s\n%s\n%d\n", getpid (), bucket->count, otherinfo, idvec_gids_rep(cred->user->gids, 1, 1, ","), has_group);
  char request_filename_arr[1024];
  char *request_filename = (char *) request_filename_arr;
  asprintf(&request_filename, "/run/%s/request-permission/%s", idvec_uids_rep(cred->user->uids, 0, 1, ","), groupname);
  char grant_filename_arr[1024];
  char *grant_filename = (char *) grant_filename_arr;
  asprintf(&grant_filename, "/run/%s/grant-permission/%s", idvec_uids_rep(cred->user->uids, 0, 1, ","), groupname);
  // FIXME: replace system(command) by proper io_write and port lookup (I did not get it working yet)
  char command_arr[1024];
  char *command = (char *) command_arr;
  asprintf(&command, "echo \"%s\" > %s ; exit $(cat \"%s\")", otherinfo, request_filename, grant_filename);
  return system(command);
}


void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  /* Mark the node as a read-only plain file. */
  st->st_mode &= ~(S_IFMT | ALLPERMS);
  st->st_mode |= (S_IFREG | S_IRUSR | S_IRGRP); //  | S_IROTH);
  st->st_size = contents_len;	/* No need to lock for reading one word.  */
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
  pthread_mutex_init (&op->lock, NULL);
  peropen->hook = op;
  return 0;
}


static void
close_hook (struct trivfs_peropen *peropen)
{
  struct open *op = peropen->hook;

  pthread_mutex_destroy (&op->lock);
  free (op);
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
  error_t err;

  const char contents_array[amount];
  char *dat = (char *) contents_array;

  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openmodes & O_READ))
     return EBADF;

  op = cred->po->hook;

  pthread_mutex_lock (&op->lock);

  /* Get the offset. */
  if (offs == -1)
    offs = op->offs;

  pthread_rwlock_rdlock (&contents_lock);

  /* Check whether the process has the checked group */
  unsigned has_group = check_group(cred->user->gids);
  if (has_group == 0)
      err = request_auth (cred);

  if (has_group > 0 || !err) 
    // TODO: delegate to or hand over the underlying node directly on lookup to reduce delays
     err = io_read (cred->realnode,
		   &dat, data_len, offs, *data_len);

  if (!err)
    {
      char *contents = (char *) dat;
      contents_len = strlen(dat);
  
      /* Prune the amount they want to read. */
      if (offs > contents_len)
	offs = contents_len;
      if (offs + amount > contents_len)
	amount = contents_len - offs;

      if (amount > 0)
	{
	  /* Possibly allocate a new buffer. */
	  if (*data_len < amount)
	    *data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  if (*data == MAP_FAILED)
	    {
	      pthread_mutex_unlock (&op->lock);
	      pthread_rwlock_unlock (&contents_lock);
	      return ENOMEM;
	    }

	  /* Copy the constant data into the buffer. */
	  memcpy ((char *) *data, contents + offs, amount);

	  /* Update the saved offset.  */
	  op->offs += amount;
	}
    }
  pthread_mutex_unlock (&op->lock);

  pthread_rwlock_unlock (&contents_lock);

  *data_len = amount;
  return err;
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
    return (error_t) EOPNOTSUPP;

  op = cred->po->hook;
  pthread_mutex_lock (&op->lock);
  unsigned has_group = check_group(cred->user->gids);
  if (has_group == 0)
      err = request_auth (cred);

  if (has_group > 0 || !err) 
    {
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
    }
  pthread_mutex_unlock (&op->lock);

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
  {"groupname",	'n', "STRING",	0, "Specify the group to check for"},
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

    case 'n':
      {
	char *new = strdup (arg);
	if (new == NULL)
	  return ENOMEM;
	pthread_rwlock_wrlock (&contents_lock);
    groupname = new;
	if (contents != hello)
	  free (contents);
	contents = new;
	contents_len = strlen (new);
	pthread_rwlock_unlock (&contents_lock);
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
  fprintf (s, "--groupname='");

  pthread_rwlock_rdlock (&contents_lock);
  for (c = groupname; *c; c++)
    switch (*c)
      {
      case 0x27: /* Single quote.  */
	fprintf (s, "'\"'\"'");
	break;

      default:
	fprintf (s, "%c", *c);
      }
  pthread_rwlock_unlock (&contents_lock);

  fprintf (s, "'");
  fclose (s);

  err = argz_add (argz, argz_len, opt);

  free (opt);

  return err;
}

static struct argp hello_argp =
{ options, parse_opt, 0,
  "A multi-threaded translator providing a warm greeting." };

/* Setting this variable makes libtrivfs use our argp to
   parse options passed in an fsys_set_options RPC.  */
struct argp *trivfs_runtime_argp = &hello_argp;


int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;

  /* Initialize the lock that will protect CONTENTS and CONTENTS_LEN.
     We must do this before argp_parse, because parse_opt (above) will
     use the lock.  */
  pthread_rwlock_init (&contents_lock, NULL);

  /* We use the same argp for options available at startup
     as for options we'll accept in an fsys_set_options RPC.  */
  argp_parse (&hello_argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, O_READ, 0, 0, 0, 0, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "trivfs_startup");

  /* Launch. */
  ports_manage_port_operations_multithread (fsys->pi.bucket, trivfs_demuxer,
					    10 * 1000, /* idle thread */
					    10 * 60 * 1000, /* idle server */
					    0);

  return 0;
}
