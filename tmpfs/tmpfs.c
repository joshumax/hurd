/* Main program and global state for tmpfs.
   Copyright (C) 2000,01,02 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <argp.h>
#include <argz.h>
#include <string.h>
#include <inttypes.h>
#include <error.h>

#include "tmpfs.h"
#include <limits.h>
#include <version.h>
#include <fcntl.h>
#include <hurd.h>
#include <hurd/paths.h>

char *diskfs_server_name = "tmpfs";
char *diskfs_server_version = HURD_VERSION;
char *diskfs_extra_version = "GNU Hurd";
char *diskfs_disk_name = "none";

/* We ain't got to show you no stinkin' sync'ing.  */
int diskfs_default_sync_interval = 0;

/* We must supply some claimed limits, though we don't impose any new ones.  */
int diskfs_link_max = (1ULL << (sizeof (nlink_t) * CHAR_BIT)) - 1;
int diskfs_name_max = 255;	/* dirent d_namlen limit */
int diskfs_maxsymlinks = 8;

/* Yeah, baby, we do it all!  */
int diskfs_shortcut_symlink = 1;
int diskfs_shortcut_chrdev = 1;
int diskfs_shortcut_blkdev = 1;
int diskfs_shortcut_fifo = 1;
int diskfs_shortcut_ifsock = 1;

struct node *diskfs_root_node;
mach_port_t default_pager;

off_t tmpfs_page_limit, tmpfs_space_used;
mode_t tmpfs_root_mode = -1;

error_t
diskfs_set_statfs (struct statfs *st)
{
  fsblkcnt_t pages;

  st->f_type = FSTYPE_MEMFS;
  st->f_fsid = getpid ();

  st->f_bsize = vm_page_size;
  st->f_blocks = tmpfs_page_limit;

  st->f_files = __atomic_load_n (&num_files, __ATOMIC_RELAXED);
  pages = round_page (get_used ()) / vm_page_size;

  st->f_bfree = pages < tmpfs_page_limit ? tmpfs_page_limit - pages : 0;
  st->f_bavail = st->f_bfree;
  st->f_ffree = st->f_bavail / sizeof (struct disknode); /* Well, sort of.  */

  return 0;
}


error_t
diskfs_set_hypermetadata (int wait, int clean)
{
  /* All the state always just lives in core, so we have nothing to do.  */
  return 0;
}

void
diskfs_sync_everything (int wait)
{
}

error_t
diskfs_reload_global_state ()
{
  return 0;
}

int diskfs_synchronous = 0;

#define OPT_SIZE 600	/* --size */

static const struct argp_option options[] =
{
  {"mode", 'm', "MODE", 0, "Permissions (octal) for root directory"},
  {"size", OPT_SIZE, "MAX-BYTES", 0, "Maximum size"},
  {NULL,}
};

struct option_values
{
  off_t size;
  mode_t mode;
};

/* Parse the size string ARG, and set *NEWSIZE with the resulting size.  */
static error_t
parse_opt_size (const char *arg, struct argp_state *state, off_t *newsize)
{
  char *end = NULL;
  intmax_t size = strtoimax (arg, &end, 0);
  if (end == NULL || end == arg)
    {
      argp_error (state, "argument must be a number");
      return EINVAL;
    }
  if (size < 0)
    {
      argp_error (state, "negative size not meaningful");
      return EINVAL;
    }
  switch (*end)
    {
      case 'g':
      case 'G':
	size <<= 10;
      case 'm':
      case 'M':
	size <<= 10;
      case 'k':
      case 'K':
	size <<= 10;
	break;
      case '%':
	{
	  /* Set as a percentage of the machine's physical memory.  */
	  struct vm_statistics vmstats;
	  error_t err = vm_statistics (mach_task_self (), &vmstats);
	  if (err)
	    {
	      argp_error (state, "cannot find total physical memory: %s",
			  strerror (err));
	      return err;
	    }
	  size = round_page ((((vmstats.free_count
				+ vmstats.active_count
				+ vmstats.inactive_count
				+ vmstats.wire_count)
			       * vm_page_size)
			      * size + 99) / 100);
	  break;
	}
    }
  size = (off_t) size;
  if (size < 0)
    {
      argp_error (state, "size too large");
      return EINVAL;
    }

  *newsize = size;

  return 0;
}

/* Parse a command line option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  /* We save our parsed values in this structure, hung off STATE->hook.
     Only after parsing all options successfully will we use these values.  */
  struct option_values *values = state->hook;

  switch (key)
    {
    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input;
      values = malloc (sizeof *values);
      if (values == 0)
	return ENOMEM;
      state->hook = values;
      values->size = -1;
      values->mode = -1;
      break;
    case ARGP_KEY_FINI:
      free (values);
      state->hook = 0;
      break;

    case 'm':			/* --mode=OCTAL */
      {
	char *end = NULL;
	mode_t mode = strtoul (arg, &end, 8);
	if (end == NULL || end == arg)
	  {
	    argp_error (state, "argument must be an octal number");
	    return EINVAL;
	  }
	if (mode & S_IFMT)
	  {
	    argp_error (state, "invalid bits in mode");
	    return EINVAL;
	  }
	values->mode = mode;
      }
      break;

    case OPT_SIZE:		/* --size=MAX-BYTES */
      {
	error_t err = parse_opt_size (arg, state, &values->size);
	if (err)
	  return err;
      }
      break;

    case ARGP_KEY_NO_ARGS:
      if (values->size < 0)
	{
	  argp_error (state, "must supply maximum size");
	  return EINVAL;
	}
      break;

    case ARGP_KEY_ARGS:
      if (state->argv[state->next + 1] != 0)
	{
	  argp_error (state, "too many arguments");
	  return EINVAL;
	}
      else if (values->size >= 0)
	{
	  if (strcmp (state->argv[state->next], "tmpfs") != 0)
	    {
	      argp_error (state, "size specified with --size and argument is not \"tmpfs\"");
	      return EINVAL;
	    }
	}
      else
	{
	  error_t err = parse_opt_size (state->argv[state->next], state,
					&values->size);
	  if (err)
	    return err;
	}
      break;

    case ARGP_KEY_SUCCESS:
      /* All options parse successfully, so implement ours if possible.  */
      tmpfs_page_limit = values->size / vm_page_size;
      tmpfs_root_mode = values->mode;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/* Override the standard diskfs routine so we can add our own output.  */
error_t
diskfs_append_args (char **argz, size_t *argz_len)
{
  error_t err;

  /* Get the standard things.  */
  err = diskfs_append_std_options (argz, argz_len);

  if (!err)
    {
      off_t lim = tmpfs_page_limit * vm_page_size;
      char buf[100], sfx;
#define S(n, c) if ((lim & ((1 << n) - 1)) == 0) sfx = c, lim >>= n
      S (30, 'G'); else S (20, 'M'); else S (10, 'K'); else sfx = '\0';
#undef S
      snprintf (buf, sizeof buf, "%Ld%c", lim, sfx);
      err = argz_add (argz, argz_len, buf);
    }

  return err;
}

/* Handling of operations for the ports in diskfs_port_bucket, calling 
 * demuxer for each incoming message */
static void *
diskfs_thread_function (void *demuxer)
{
  static int thread_timeout = 1000 * 60 * 2; /* two minutes */
  error_t err;

  do
    {
      ports_manage_port_operations_multithread (diskfs_port_bucket,
						(ports_demuxer_type) demuxer,
						thread_timeout,
						0,
						0);
      err = diskfs_shutdown (0);
    }
  while (err);

  exit (0);
  /* NOTREACHED */
  return NULL;
}


/* Add our startup arguments to the standard diskfs set.  */
static const struct argp_child startup_children[] =
  {{&diskfs_startup_argp}, {0}};
static struct argp startup_argp = {options, parse_opt, "MAX-BYTES", "\
\v\
MAX-BYTES may be followed by k or K for kilobytes,\n\
m or M for megabytes, g or G for gigabytes.",
				   startup_children};

/* Similarly at runtime.  */
static const struct argp_child runtime_children[] =
  {{&diskfs_std_runtime_argp}, {0}};
static struct argp runtime_argp = {options, parse_opt, 0, 0, runtime_children};

struct argp *diskfs_runtime_argp = (struct argp *)&runtime_argp;



int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap, realnode, host_priv;
  pthread_t pthread_id;
  struct stat st;

  err = argp_parse (&startup_argp, argc, argv, ARGP_IN_ORDER, NULL, NULL);
  assert_perror_backtrace (err);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (2, 0, "Must be started as a translator");

  /* Get our port to the default pager.  Without that,
     we have no place to put file contents.  */
  err = get_privileged_ports (&host_priv, NULL);
  if (err == EPERM)
    {
      default_pager = file_name_lookup (_SERVERS_DEFPAGER, O_EXEC, 0);
      if (default_pager == MACH_PORT_NULL)
	error (0, errno, _SERVERS_DEFPAGER);
    }
  else if (err)
    error (0, err, "Cannot get host privileged port");
  else
    {
      err = vm_set_default_memory_manager (host_priv, &default_pager);
      mach_port_deallocate (mach_task_self (), host_priv);
      if (err)
	error (0, err, "Cannot get default pager port");
    }
  if (default_pager == MACH_PORT_NULL)
    error (0, 0, "files cannot have contents with no default pager port");

  /* Initialize the diskfs library.  Must come before any other diskfs call. */
  err = diskfs_init_diskfs ();
  if (err)
    error (4, err, "init");

  err = diskfs_alloc_node (0, S_IFDIR, &diskfs_root_node);
  if (err)
    error (4, err, "cannot create root directory");

  /* Like diskfs_spawn_first_thread. But do it manually, without timeout */
  err = pthread_create (&pthread_id, NULL, diskfs_thread_function,
			diskfs_demuxer);
  if (!err)
    pthread_detach (pthread_id);
  else
    {
      errno = err;
      perror ("pthread_create");
    }

  /* Now that we are all set up to handle requests, and diskfs_root_node is
     set properly, it is safe to export our fsys control port to the
     outside world.  */
  realnode = diskfs_startup_diskfs (bootstrap, 0);
  diskfs_root_node->dn_stat.st_mode = S_IFDIR;

  /* Propagate permissions, owner, etc. from underlying node to
     the root directory of the new (empty) filesystem.  */
  err = io_stat (realnode, &st);
  if (err)
    {
      error (0, err, "cannot stat underlying node");
      if (tmpfs_root_mode == -1)
	diskfs_root_node->dn_stat.st_mode |= 0777 | S_ISVTX;
      else
	diskfs_root_node->dn_stat.st_mode |= tmpfs_root_mode;
      diskfs_root_node->dn_set_ctime = 1;
      diskfs_root_node->dn_set_mtime = 1;
      diskfs_root_node->dn_set_atime = 1;
    }
  else
    {
      if (tmpfs_root_mode == -1)
	{
	  diskfs_root_node->dn_stat.st_mode |= st.st_mode &~ S_IFMT;
	  if (S_ISREG (st.st_mode) && (st.st_mode & 0111) == 0)
	    /* There are no execute bits set, as by default on a plain file.
	       For the virtual directory, set execute bits where read bits are
	       set on the underlying plain file.  */
	    diskfs_root_node->dn_stat.st_mode |= (st.st_mode & 0444) >> 2;
	}
      else
	diskfs_root_node->dn_stat.st_mode |= tmpfs_root_mode;
      diskfs_root_node->dn_stat.st_uid = st.st_uid;
      diskfs_root_node->dn_stat.st_author = st.st_author;
      diskfs_root_node->dn_stat.st_gid = st.st_gid;
      diskfs_root_node->dn_stat.st_atim = st.st_atim;
      diskfs_root_node->dn_stat.st_mtim = st.st_mtim;
      diskfs_root_node->dn_stat.st_ctim = st.st_ctim;
      diskfs_root_node->dn_stat.st_flags = st.st_flags;
    }
  diskfs_root_node->dn_stat.st_mode &= ~S_ITRANS;
  diskfs_root_node->dn_stat.st_mode |= S_IROOT;
  diskfs_root_node->dn_stat.st_nlink = 2;

  /* We must keep the REALNODE send right to remain the active
     translator for the underlying node.  */

  pthread_mutex_unlock (&diskfs_root_node->lock);

  /* and so we die, leaving others to do the real work.  */
  pthread_exit (NULL);
  /* NOTREACHED */
  return 0;
}
