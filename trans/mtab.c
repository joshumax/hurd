/* This is an mtab translator.

   Copyright (C) 2013,14 Free Software Foundation, Inc.

   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include <argp.h>
#include <argz.h>
#include <error.h>
#include <fcntl.h>
#include <hurd.h>
#include <hurd/trivfs.h>
#include <inttypes.h>
#include <mntent.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <version.h>

#include "fsys_U.h"

static char *target_path = NULL;
static int insecure = 0;
static int all_translators = 0;

/* Our control port.  */
struct trivfs_control *control;

/* These kind of objects are created and populated in the open_hook.
   They keep track of the content and file position of the client.  */
struct mtab
{
  pthread_mutex_t lock;
  char *contents;
  size_t contents_len;
  off_t offs;
};

const char *argp_program_version = STANDARD_HURD_VERSION (mtab);

static const struct argp_option options[] =
{
  {"insecure", 'I', 0, 0,
   "Follow translators not bound to nodes owned by you or root"},
  {"all-translators", 'A', 0, 0,
   "List all translators, even those that are probably not "
   "filesystem translators"},
  {}
};

/* Parse a command line option.	 */
error_t parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'I':
      insecure = 1;
      break;

    case 'A':
      all_translators = 1;
      break;

    case ARGP_KEY_ARG:
      target_path = realpath (arg, NULL);
      if (! target_path)
	argp_error (state, "Error while canonicalizing path");
      break;

    case ARGP_KEY_NO_ARGS:
      argp_usage (state);
      return EINVAL;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static struct argp argp =
  {
    options,
    parse_opt,
    "TARGET\tFile name of a node with an active translator",
    "A translator providing mtab compatible information about active "
    "and passive translators below TARGET.",
  };

/* This will be called from libtrivfs to help construct the answer
   to an fsys_get_options RPC.	*/
error_t
trivfs_append_args (struct trivfs_control *fsys,
		    char **argz, size_t *argz_len)
{
  error_t err;

  if (insecure)
    {
      err = argz_add (argz, argz_len, target_path);
      if (err)
	return err;
    }

  err = argz_add (argz, argz_len, target_path);
  return err;
}

/* Setting this variable makes libtrivfs use our argp to
   parse options passed in an fsys_set_options RPC.  */
struct argp *trivfs_runtime_argp = &argp;

/* Authentication of the current process.  */
uid_t *uids;
gid_t *gids;
size_t uids_len, gids_len;

/* Initialize and populate the uids and gids vectors.  */
error_t
get_credentials (void)
{
  /* Fetch uids...  */
  uids_len = geteuids (0, 0);
  if (uids_len < 0)
    return errno;

  uids = malloc (uids_len * sizeof (uid_t));
  if (! uids)
    return ENOMEM;

  uids_len = geteuids (uids_len, uids);
  if (uids_len < 0)
    return errno;

  /* ... and gids.  */
  gids_len = getgroups (0, 0);
  if (gids_len < 0)
    return errno;

  gids = malloc (gids_len * sizeof (gid_t));
  if (! uids)
    return ENOMEM;

  gids_len = getgroups (gids_len, gids);
  if (gids_len < 0)
    return errno;

  return 0;
}

/* Check if the given struct stat describes a node owned by the
   current user.  */
int
is_owner (io_statbuf_t *st)
{
  int found = 0;
  for (size_t i = 0; i < uids_len; i++)
    if (uids[i] == st->st_uid)
      {
	found = 1;
	break;
      }

  if (! found)
    return 0;

  found = 0;
  for (size_t i = 0; i < gids_len; i++)
    if (gids[i] == st->st_gid)
      {
	found = 1;
	break;
      }

  return found;
}

error_t
mtab_populate (struct mtab *mtab, const char *path, int insecure);

error_t
argz_add_device (char **options, size_t *options_len, const char *device);

error_t
map_device_to_path (const char *device, char **path);

int
main (int argc, char *argv[])
{
  error_t err;

  err = argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);
  if (err)
    error (1, err, "argument parsing");

  err = get_credentials ();
  if (err)
    error (2, err, "getting credentials");

  mach_port_t bootstrap;
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap != MACH_PORT_NULL)
    {
      /* Started as a translator.  */

      auth_t nullauth;
      err = auth_makeauth (getauth (),
			   NULL, MACH_MSG_TYPE_COPY_SEND, 0,
			   NULL, 0,
			   NULL, 0,
			   NULL, 0,
			   NULL, 0,
			   &nullauth);
      if (err)
        error (3, err, "dropping credentials");

      err = setauth (nullauth);
      if (err)
        error (3, err, "dropping credentials");

      /* Reply to our parent.  */
      err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &control);
      mach_port_deallocate (mach_task_self (), bootstrap);
      if (err)
	error (4, err, "trivfs_startup");

      /* Launch.  */
      ports_manage_port_operations_multithread (control->pi.bucket,
                                                trivfs_demuxer,
                                                /* idle thread timeout */
                                                30 * 1000,
                                                /* idle server timeout */
                                                0,
                                                NULL);
    }
  else
    {
      /* One-shot mode.	 */
      struct mtab mtab = { NULL, 0, 0 };
      err = mtab_populate (&mtab, target_path, insecure);
      if (err)
	error (5, err, "%s", target_path);

      if (mtab.contents)
	printf ("%s", mtab.contents);
    }

  return 0;
}

error_t
mtab_add_entry (struct mtab *mtab, const char *entry, size_t length)
{
  char *p = realloc (mtab->contents, mtab->contents_len + length + 1);
  if (! p)
    return ENOMEM;

  memcpy (&p[mtab->contents_len], entry, length);

  mtab->contents = p;
  mtab->contents_len += length;

  /* Zero-terminate contents so that we can also interpret it as
     string.  */
  mtab->contents[mtab->contents_len] = '\0';

  return 0;
}

/* Check whether the given NODE is a directory on a filesystem
   translator.  */
static boolean_t
is_filesystem_translator (file_t node)
{
  error_t err;
  char *data = NULL;
  size_t datacnt = 0;
  int amount;
  err = dir_readdir (node, &data, &datacnt, 0, 1, 0, &amount);
  if (data != NULL && datacnt > 0)
    vm_deallocate (mach_task_self (), (vm_address_t) data, datacnt);

  /* Filesystem translators return either no error, or, if NODE has
     not been looked up with O_READ, EBADF to dir_readdir
     requests.  */
  switch (err)
    {
    case 0:
    case EBADF:
      return TRUE;
    default:
      return FALSE;
    }
}

/* Populates the given MTAB object with the information for PATH.  If
   INSECURE is given, also follow translators bound to nodes not owned
   by root or the current user.  */
/* XXX split up */
error_t
mtab_populate (struct mtab *mtab, const char *path, int insecure)
{
  error_t err = 0;

  /* These resources are freed in the epilogue.	 */
  file_t node = MACH_PORT_NULL;
  char *argz = NULL;
  size_t argz_len = 0;
  char **argv = NULL;
  char *type = NULL;
  char *options = NULL;
  size_t options_len = 0;
  char *src = NULL;
  char *entry = NULL;
  size_t entry_len = 0;
  char *children = NULL;
  size_t children_len = 0;

  if (! insecure)
    {
      /* Get the underlying node.  */
      node = file_name_lookup (path, O_NOTRANS, 0666);
      if (node == MACH_PORT_NULL)
        {
          err = errno;
          goto errout;
        }

      /* Check who owns the node the translator is bound to.  */
      io_statbuf_t st;
      err = io_stat (node, &st);
      if (err)
	goto errout;

      if (st.st_uid != 0 && st.st_gid != 0 && ! is_owner (&st))
	{
	  err = EPERM;
	  goto errout;
	}

      mach_port_deallocate (mach_task_self (), node);
    }

  /* (Re-)do the lookup without O_NOTRANS to get the root node.  */
  node = file_name_lookup (path, 0, 0666);
  if (node == MACH_PORT_NULL)
    {
      err = errno;
      goto errout;
    }

  if (! (all_translators || is_filesystem_translator (node)))
    {
      err = 0;
      goto errout;
    }

  /* Query its options.	 */
  err = file_get_fs_options (node, &argz, &argz_len);
  if (err)
    {
      if (err == EOPNOTSUPP)
	err = 0; /* There's not much we could do then.	*/

      goto errout;
    }

  size_t count = argz_count (argz, argz_len);
  argv = malloc ((count + 1) * sizeof (char *));
  if (! argv)
    {
      err = ENOMEM;
      goto errout;
    }

  argz_extract (argz, argz_len, argv);

  type = strdup (argv[0]);
  if (! type)
    {
      err = ENOMEM;
      goto errout;
    }

  for (int i = 1; i < count - 1; i++)
    {
      char *v = argv[i];

      if (*v == '-')
	v++;
      if (*v == '-')
	v++;

      err = argz_add (&options, &options_len, v);
      if (err)
	goto errout;
    }

  err = argz_add_device (&options, &options_len, argv[count - 1]);
  if (err)
    goto errout;

  argz_stringify (options, options_len, ',');

  string_t source;
  err = fsys_get_source (node, source);
  if (err)
    {
      if (err == EOPNOTSUPP)
	{
	  /* Guess based on the last argument.	*/
	  err = map_device_to_path (argv[count - 1], &src);
	  if (err)
	    goto errout;
	}
      else
	goto errout;
    }
  else
    src = source;

  entry_len = asprintf (&entry, "%s %s %s %s 0 0\n", src, path, type,
			options? options: MNTOPT_DEFAULTS);
  if (! entry)
    {
      err = ENOMEM;
      goto errout;
    }

  err = mtab_add_entry (mtab, entry, entry_len);
  if (err)
    goto errout;

  /* path has an active translator, query its children.	 */
  err = fsys_get_children (node, &children, &children_len);
  if (err == EOPNOTSUPP)
    {
      err = 0;
      children_len = 0;
    }

  if (err)
    goto errout;

  if (children_len)
    for (char *c = children; c; c = argz_next (children, children_len, c))
      {
	char *p = NULL;
	asprintf (&p, "%s%s%s",
		  path,
		  path[strlen (path) - 1] == '/'? "": "/",
		  c);
	if (! p)
	  {
	    err = ENOMEM;
	    goto errout;
	  }

	err = mtab_populate (mtab, p, insecure);
	if (err)
	  {
	    /* There is really not much we can do about errors here.  */
	    error (0, err, "%s", p);
	    err = 0;
	  }

	free (p);
      }

 errout:
  if (node != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), node);

  if (argz)
    vm_deallocate (mach_task_self (), (vm_address_t) argz, argz_len);

  free (argv);
  free (type);
  free (options);

  if (src != source)
    free (src);

  free (entry);

  if (children)
    vm_deallocate (mach_task_self (), (vm_address_t) children, children_len);

  return err;
}

/* Decodes the DEVICE string into appropriate OPTIONS.  Currently only
   tmpfs-style size declarations are supported.  */
error_t
argz_add_device (char **options, size_t *options_len, const char *device)
{
  error_t err;
  char *end = NULL;
  intmax_t size = strtoimax (device, &end, 0);
  if (end == NULL || end == device)
    return 0;

  if (size < 0)
    return 0;

  switch (*end)
    {
      case 'g':
      case 'G':
      case 'm':
      case 'M':
      case 'k':
      case 'K':
	break;
    default:
      return 0;
    }

  /* device specifies a size.  */
  char *arg = NULL;
  asprintf (&arg, "size=%s", device);
  if (! arg)
    return ENOMEM;

  err = argz_add (options, options_len, arg);

  free (arg);
  return err;
}

/* Matches [hs]d\ds\d\d?.  */
int
looks_like_block_device (const char *s)
{
  size_t len = strlen (s);
  if (len != 3 && len != 5 && len != 6)
    return 0;

  return ((s[0] == 'h' || s[0] == 's') && s[1] == 'd' && isdigit (s[2]) &&
          (len == 3 || (s[3] == 's' && isdigit (s[4]) &&
                        (len == 5 || isdigit (s[5])))));
}

/* Map a device string to a file name referencing the appropriate
   device file.	 */
error_t
map_device_to_path (const char *device, char **path)
{
  if (strncmp (device, "device:", 7) == 0)
    asprintf (path, "/dev/%s", &device[7]);
  else if (strncmp (device, "/dev/", 5) == 0)
    *path = strdup (device);
  else if (looks_like_block_device (device))
    asprintf (path, "/dev/%s", device);
  else
    *path = strdup ("none");

  if (! *path)
    return ENOMEM;

  return 0;
}

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;

int trivfs_allow_open = O_READ;

int trivfs_support_read = 1;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  /* Mark the node as a read-only plain file.  */
  st->st_mode &= ~(S_IFMT | ALLPERMS);
  st->st_mode |= (S_IFREG | S_IRUSR | S_IRGRP | S_IROTH);
  st->st_size = ((struct mtab *) cred->po->hook)->contents_len;
}

error_t
trivfs_goaway (struct trivfs_control *cntl, int flags)
{
  exit (EXIT_SUCCESS);
}

static error_t
open_hook (struct trivfs_peropen *peropen)
{
  struct mtab *mtab = malloc (sizeof (struct mtab));
  if (mtab == NULL)
    return ENOMEM;

  /* Hook! */
  peropen->hook = mtab;

  /* Initialize the fields.  */
  pthread_mutex_init (&mtab->lock, NULL);
  mtab->offs = 0;
  mtab->contents = NULL;
  mtab->contents_len = 0;

  /* The mtab object is initialized, but not yet populated.  We delay
     that until that data is really needed.  This avoids the following
     problems:

     Suppose you have

     settrans -ac /foo /hurd/mtab /

     If you now access /foo, the mtab translator will walk the tree of
     all active translators starting from /.  If it visits /foo, it
     will talk to itself.  Previously the translator migitated this by
     comparing the control port of the translator with its own.  This
     does not work if you got two mtab translators like this:

     settrans -ac /foo /hurd/mtab /
     settrans -ac /bar /hurd/mtab /

     With a single-threaded mtab server this results in a dead-lock,
     with a multi-threaded server this will create more and more
     threads.

     Delaying the data generation until it is really needed cleanly
     avoids these kind of problems.  */
  return 0;
}

static void
close_hook (struct trivfs_peropen *peropen)
{
  struct mtab *op = peropen->hook;
  pthread_mutex_destroy (&op->lock);
  free (op->contents);
  free (op);
}

/* Read data from an IO object.	 If offset is -1, read from the object
   maintained file pointer.  If the object is not seekable, offset is
   ignored.  The amount desired to be read is in AMOUNT.  */
error_t
trivfs_S_io_read (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  char **data, mach_msg_type_number_t *data_len,
		  loff_t offs, mach_msg_type_number_t amount)
{
  error_t err = 0;
  struct mtab *op;

  /* Deny access if they have bad credentials.	*/
  if (! cred)
    return EOPNOTSUPP;

  if (! (cred->po->openmodes & O_READ))
    return EBADF;

  /* Get the offset.  */
  op = cred->po->hook;
  pthread_mutex_lock (&op->lock);

  if (op->contents == NULL)
    {
      err = mtab_populate (op, target_path, insecure);
      if (err)
	goto out;
    }

  if (offs == -1)
    offs = op->offs;

  /* Prune the amount they want to read.  */
  if (offs > op->contents_len)
    offs = op->contents_len;
  if (offs + amount > op->contents_len)
    amount = op->contents_len - offs;

  if (amount > 0)
    {
      /* Possibly allocate a new buffer.  */
      if (*data_len < amount)
	{
	  *data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  if (*data == MAP_FAILED)
	    {
	      err = ENOMEM;
	      goto out;
	    }
	}

      /* Copy the constant data into the buffer.  */
      memcpy ((char *) *data, op->contents + offs, amount);

      /* Update the saved offset.  */
      op->offs += amount;
    }

  *data_len = amount;
 out:
  pthread_mutex_unlock (&op->lock);
  return err;
}


/* Change current read/write offset */
error_t
trivfs_S_io_seek (struct trivfs_protid *cred,
		  mach_port_t reply, mach_msg_type_name_t reply_type,
		  off_t offs, int whence, off_t *new_offs)
{
  error_t err = 0;
  if (! cred)
    return EOPNOTSUPP;

  struct mtab *op = cred->po->hook;
  pthread_mutex_lock (&op->lock);

  if (op->contents == NULL)
    {
      err = mtab_populate (op, target_path, insecure);
      if (err)
	goto out;
    }

  switch (whence)
    {
    case SEEK_CUR:
      offs += op->offs;
      goto check;
    case SEEK_END:
      offs += op->contents_len;
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

 out:
  pthread_mutex_unlock (&op->lock);
  return err;
}

/* If this variable is set, it is called every time a new peropen
   structure is created and initialized.  */
error_t (*trivfs_peropen_create_hook)(struct trivfs_peropen *) = open_hook;

/* If this variable is set, it is called every time a peropen structure
   is about to be destroyed.  */
void (*trivfs_peropen_destroy_hook) (struct trivfs_peropen *) = close_hook;

/* Tell how much data can be read from the object without blocking for
   a "long time" (this should be the same meaning of "long time" used
   by the nonblocking flag.  */
kern_return_t
trivfs_S_io_readable (struct trivfs_protid *cred,
		      mach_port_t reply, mach_msg_type_name_t replytype,
		      mach_msg_type_number_t *amount)
{
  error_t err = 0;
  if (!cred)
    return EOPNOTSUPP;

  if (!(cred->po->openmodes & O_READ))
    return EINVAL;

  struct mtab *op = cred->po->hook;
  pthread_mutex_lock (&op->lock);

  if (op->contents == NULL)
    {
      error_t err = mtab_populate (op, target_path, insecure);
      if (err)
	goto out;
    }

  *amount = op->contents_len - op->offs;
 out:
  pthread_mutex_unlock (&op->lock);
  return err;
}

/* SELECT_TYPE is the bitwise OR of SELECT_READ, SELECT_WRITE, and SELECT_URG.
   Block until one of the indicated types of i/o can be done "quickly", and
   return the types that are then available.  ID_TAG is returned as passed; it
   is just for the convenience of the user in matching up reply messages with
   specific requests sent.  */
kern_return_t
trivfs_S_io_select (struct trivfs_protid *cred,
		    mach_port_t reply, mach_msg_type_name_t replytype,
		    int *type)
{
  if (!cred)
    return EOPNOTSUPP;

  if (((*type & SELECT_READ) && !(cred->po->openmodes & O_READ))
      || ((*type & SELECT_WRITE) && !(cred->po->openmodes & O_WRITE)))
    return EBADF;

  *type &= ~SELECT_URG;
  return 0;
}

kern_return_t
trivfs_S_io_select_timeout (struct trivfs_protid *cred,
			    mach_port_t reply, mach_msg_type_name_t replytype,
			    struct timespec ts,
			    int *type)
{
  return trivfs_S_io_select (cred, reply, replytype, type);
}

/* These four routines modify the O_APPEND, O_ASYNC, O_FSYNC, and
   O_NONBLOCK bits for the IO object. In addition, io_get_openmodes
   will tell you which of O_READ, O_WRITE, and O_EXEC the object can
   be used for.	 The O_ASYNC bit affects icky async I/O; good async
   I/O is done through io_async which is orthogonal to these calls. */

kern_return_t
trivfs_S_io_get_openmodes (struct trivfs_protid *cred,
			   mach_port_t reply, mach_msg_type_name_t replytype,
			   int *bits)
{
  if (!cred)
    return EOPNOTSUPP;

  *bits = cred->po->openmodes;
  return 0;
}

error_t
trivfs_S_io_set_all_openmodes(struct trivfs_protid *cred,
			      mach_port_t reply,
			      mach_msg_type_name_t replytype,
			      int mode)
{
  if (!cred)
    return EOPNOTSUPP;

  return 0;
}

kern_return_t
trivfs_S_io_set_some_openmodes (struct trivfs_protid *cred,
				mach_port_t reply,
				mach_msg_type_name_t replytype,
				int bits)
{
  if (!cred)
    return EOPNOTSUPP;

  return 0;
}

kern_return_t
trivfs_S_io_clear_some_openmodes (struct trivfs_protid *cred,
				  mach_port_t reply,
				  mach_msg_type_name_t replytype,
				  int bits)
{
  if (!cred)
    return EOPNOTSUPP;

  return 0;
}
