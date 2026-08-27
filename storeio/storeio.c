/* A translator for doing I/O to stores

   Copyright (C) 1995,96,97,98,99,2000,01,02,26 Free Software Foundation, Inc.
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
#include <stdlib.h>
#include <error.h>
#include <assert-backtrace.h>
#include <fcntl.h>
#include <argp.h>
#include <argz.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <dirent.h>

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/store.h>
#include <version.h>

#include "dev.h"
#include "libnetfs/fs_S.h"
#include "libnetfs/io_S.h"
#include "libnetfs/fsys_S.h"
#include "libnetfs/fsys_reply_U.h"

static struct argp_option options[] =
{
  {"readonly", 'r', 0,	  0,"Disallow writing"},
  {"writable", 'w', 0,	  0,"Allow writing"},
  {"no-cache", 'c', 0,	  0,"Never cache data--user io does direct device io"},
  {"no-file-io", 'F', 0,  0,"Never perform io via plain file io RPCs"},
  {"no-fileio",  0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"enforced",  'e', 0,	  0,"Never reveal underlying devices, even to root"},
#ifdef DEBUG
  {"debug", 'd', "FILE", 0, "Enable debug and write debug statements to"
   " FILE.  The FILE must be located outside the translator directory."},
#endif
  {"rdev",     'n', "ID", 0,
   "The stat rdev number for this node; may be either a"
   " single integer, or of the form MAJOR,MINOR"},
  {0}
};
static const char doc[] = "Translator for devices and other stores";

const char *argp_program_version = STANDARD_HURD_VERSION (storeio);

char *netfs_server_name = "storeio";
char *netfs_server_version = HURD_VERSION;
int netfs_maxsymlinks = 0; /* arbitrary */

char *debug_file_name = NULL;
FILE *debug_file;
pthread_mutex_t debug_lock;

error_t
create_node (struct node **node, char *name, struct node *dir)
{
  error_t err;
  struct netnode *netnode = malloc (sizeof (struct netnode));
  if (!netnode)
    {
      err = errno;
      goto return_error;
    }

  struct node *new_node = netfs_make_node (netnode);
  if (!new_node)
    {
      err = errno;
      goto free_netnode;
    }

  static ino_t id = 1;
  io_statbuf_t statbuf = {
    .st_fstype = FSTYPE_MISC,
    .st_fsid = storeio_stat.pid,
    .st_dev = storeio_stat.pid,
    .st_rdev = storeio_stat.pid,
    .st_uid = storeio_stat.uid,
    .st_author = storeio_stat.uid,
    .st_gid = storeio_stat.gid,
    .st_mode = storeio_stat.mode,
    .st_ino = id++,
    .st_nlink = 1,
    .st_blksize = 0,
    .st_size = 0,
    .st_blocks = 1,
    .st_gen = 0
  };
  new_node->nn_stat = statbuf;
  new_node->next = NULL;
  new_node->prevp = NULL;

  struct dev *dev = malloc (sizeof (struct dev));
  if (!dev)
    {
      err = errno;
      goto free_new_node;
    }

  memset (dev, 0, sizeof (struct dev));
  pthread_mutex_init (&dev->lock, NULL);
  new_node->nn->dev = dev;
  new_node->nn->name = name;
  new_node->nn->entries = NULL;
  new_node->nn->entries_num = 0;

  if (dir)
    netfs_nref (dir);

  fshelp_touch (&new_node->nn_stat, TOUCH_ATIME|TOUCH_CTIME|TOUCH_MTIME,
                storeio_stat.current_time);

  *node = new_node;

  debug ("create_node (name: %s, dir: %p):\n", name, dir);
  debug ("*node: %p\n", *node);
  debug ("create_node return: 0");
  return 0;

 free_new_node:
  free (new_node);

 free_netnode:
  free (netnode);

 return_error:
  *node = NULL;

  debug ("create_node (name: %s, dir: %p):\n", name, dir);
  debug ("*node: %p\n", *node);
  debug ("create_node return: %d", err);
  return err;
}

static inline void
change_node_mode (struct node *node)
{
  node->nn_stat.st_mode &= S_IFMT;

  if (storeio_stat.inhibit_cache)
    {
      node->nn_stat.st_mode |= S_IFCHR;
      return;
    }

  if (node->nn->dev->store && node->nn->dev->store->block_size == 1)
    node->nn_stat.st_mode |= S_IFCHR;
  else
    node->nn_stat.st_mode |= S_IFBLK;
}

error_t
check_dev (struct node *node, struct store *store, int flags)
{
  if (dev_is_readonly (node->nn->dev) && (flags & O_WRITE))
    {
      debug ("check_dev (node: %p, store: %p, flags: %d):\n",
             node, store, flags);
      debug ("dev_is_readonly (node->nn->dev) && (flags & O_WRITE)\n");
      debug ("check_dev return: EROFS\n");
      return EROFS;
    }

  struct dev *dev = node->nn->dev;
  pthread_mutex_lock (&dev->lock);
  if (!dev->store)
    {
      error_t err;

      if (store)
        err = dev_open_from_store (dev, store);
      else
        err = dev_open (dev, storeio_stat.store_name);

      if (err)
        {
          pthread_mutex_unlock (&dev->lock);

          debug ("check_dev (node: %p, store: %p, flags: %d):\n", node,
                 store, flags);
          if ((flags & (O_READ | O_WRITE)) == 0)
            {
              debug ("dev open err: %d, but we are not opening the file for"
                     " reading or writing, just ignore the error and exit"
                     " the function. XXX\n", err);
              debug ("check_dev return: 0\n");
              return 0;
            }

          debug ("dev open return err: %d\n", err);
          debug ("check_dev return: %d\n", err);
          return err;
        }

      node->nn_stat.st_size = dev->store->size;

      if (dev->store->block_size > 1)
        node->nn_stat.st_blksize = dev->store->block_size;

      if (node != netfs_root_node)
        change_node_mode (node);
    }
  pthread_mutex_unlock (&dev->lock);

  return 0;
}

error_t __attribute__ ((weak))
create_partitions (void)
{
  return ENOTDIR;
}

static error_t
create_storeio (void)
{
  debug ("create_storeio:\n");
  error_t err = maptime_map (1, 0, &storeio_stat.current_time);
  if (err)
    {
      err = maptime_map (0, 0, &storeio_stat.current_time);
      if (err)
        return err;
    }

  storeio_stat.pid = getpid ();
  storeio_stat.uid = getuid ();
  storeio_stat.gid = getgid ();

  storeio_stat.mode = storeio_stat.readonly ? 0444 : 0644;

  err = create_node (&netfs_root_node, NULL, NULL);
  if (err)
    {
      debug ("create_node return err: %d\n", err);
      debug ("create_storeio return: %d\n", err);
      return err;
    }

  netfs_root_node->nn_stat.st_nlink = 2;

  debug ("create_storeio return: 0\n");
  return 0;
}

struct storeio_stat storeio_stat;

/* Parse a single option.  */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct store_argp_params *store_params = state->input;

  switch (key)
    {
    case 'r': storeio_stat.readonly = 1; break;
    case 'w': storeio_stat.readonly = 0; break;

    case 'c': storeio_stat.inhibit_cache = 1; break;
    case 'e': storeio_stat.enforced = 1; break;
    case 'F': storeio_stat.no_fileio = 1; break;
#ifdef DEBUG
    case 'd': debug_file_name = arg; break;
#endif

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

	storeio_stat.rdev = rdev;
      }
      break;

    case ARGP_KEY_INIT:
      /* Now store_argp's parser will get to initialize its state.
	 The default_type member is our input parameter to it.  */
      memset (&storeio_stat, 0, sizeof (struct storeio_stat));
      memset (store_params, 0, sizeof (struct store_argp_params));
      store_params->default_type = "device";
      store_params->store_optional = 1;
      state->child_inputs[0] = store_params;
      break;

    case ARGP_KEY_SUCCESS:
      storeio_stat.store_name = store_params->result;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const struct argp_child argp_kids[] = { { &store_argp }, {0} };
static const struct argp argp = { options, parse_opt, 0, doc, argp_kids };

mach_port_t underlying_node;

int
main (int argc, char *argv[])
{
  struct store_argp_params store_params;
  argp_parse (&argp, argc, argv, 0, 0, &store_params);

  mach_port_t bootstrap;
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (2, 0, "Must be started as a translator");

  netfs_init ();

  underlying_node = netfs_startup (bootstrap, O_READ);
  io_statbuf_t underlying_stat;

  error_t err = io_stat (underlying_node, &underlying_stat);
  if (err)
    error (1, err, "Cannot stat underlying node");

#ifdef DEBUG
  if (debug_file_name)
    {
      debug_file = fopen (debug_file_name, "a");
      setbuf (debug_file, NULL);
      pthread_mutex_init (&debug_lock, NULL);
    }
#endif

  debug ("\n---------------start main---------------\n");

  err = create_storeio ();
  if (err)
    error (1, err, "Cannot creare storeio");

  netfs_root_node->nn_stat = underlying_stat;
  netfs_root_node->nn_stat.st_mode =
    S_IFDIR | (underlying_stat.st_mode & ~S_IFMT & ~S_ITRANS);
  debug ("netfs_root_node->nn_stat.st_mode: %d\n",
         netfs_root_node->nn_stat.st_mode);

  /* Launch. */
  debug ("netfs_server_loop()...\n");
  netfs_server_loop ();

  return 0;
}

error_t
netfs_append_args (char **argz, size_t *argz_len)
{
  error_t err = 0;

  if (storeio_stat.rdev != (dev_t) 0)
    {
      char buf[40];
      snprintf (buf, sizeof (buf), "--rdev=%d,%d",
                gnu_dev_major (storeio_stat.rdev),
                gnu_dev_minor (storeio_stat.rdev));

      err = argz_add (argz, argz_len, buf);
    }

  if (!err && storeio_stat.inhibit_cache)
    err = argz_add (argz, argz_len, "--no-cache");

  if (!err && storeio_stat.enforced)
    err = argz_add (argz, argz_len, "--enforced");

  if (!err && storeio_stat.no_fileio)
    err = argz_add (argz, argz_len, "--no-file-io");

  if (!err)
    err = argz_add (argz, argz_len,
                    storeio_stat.readonly ? "--readonly" : "--writable");

  if (!err)
    err = store_parsed_append_args (storeio_stat.store_name, argz, argz_len);

  return err;
}

static error_t
node_open_create (struct peropen *po)
{
  error_t err = 0;
  struct dev *dev = po->np->nn->dev;
  if (dev->store)
    {
      pthread_mutex_lock (&dev->lock);
      if (dev->nperopens++ == 0)
        err = store_clear_flags (dev->store, STORE_INACTIVE);
      pthread_mutex_unlock (&dev->lock);
    }

  return err;
}

error_t (*netfs_peropen_create_hook) (struct peropen *po) = node_open_create;

static void
node_open_destroy (struct peropen *po)
{
  struct dev *dev = po->np->nn->dev;
  pthread_mutex_lock (&dev->lock);
  if (--dev->nperopens == 0)
    store_clear_flags (dev->store, STORE_INACTIVE);
  pthread_mutex_unlock (&dev->lock);
}

void (*netfs_peropen_destroy_hook) (struct peropen *po) = node_open_destroy;

inline static void
devs_lock (void)
{
  pthread_mutex_lock (&netfs_root_node->nn->dev->lock);
  for (size_t i = 0; i < netfs_root_node->nn->entries_num; ++i)
    pthread_mutex_lock (&netfs_root_node->nn->entries[i]->nn->dev->lock);
}

inline static void
devs_unlock (void)
{
  pthread_mutex_unlock (&netfs_root_node->nn->dev->lock);
  for (size_t i = 0; i < netfs_root_node->nn->entries_num; ++i)
    pthread_mutex_unlock (&netfs_root_node->nn->entries[i]->nn->dev->lock);
}

kern_return_t
netfs_S_fsys_goaway (struct netfs_control *pt,
                     mach_port_t reply,
                     mach_msg_type_name_t reply_type,
                     int flags)
{
  debug ("netfs_S_fsys_goaway enter\n");
  if (!pt)
    {
      debug ("!pt");
      debug ("netfs_S_fsys_goaway return: EOPNOTSUPP\n");
      return EOPNOTSUPP;
    }

  if ((flags & FSYS_GOAWAY_UNLINK)
      && S_ISDIR (netfs_root_node->nn_stat.st_mode))
    {
      debug ("(flags & FSYS_GOAWAY_UNLINK) && S_ISDIR"
             " (netfs_root_node->nn_stat.st_mode\n");
      debug ("netfs_S_fsys_goaway return: EBUSY\n");
      return EBUSY;
    }

  devs_lock ();

  int force = flags & FSYS_GOAWAY_FORCE;
  error_t err = ports_inhibit_class_rpcs (netfs_protid_class);
  if (err == EINTR || (err && !force))
    {
      debug ("err == EINTR || (err && !force)\n");
      devs_unlock ();
      debug ("netfs_S_fsys_goaway return: %d\n", err);
      return err;
    }

  int nosync = flags & FSYS_GOAWAY_NOSYNC;
  if (force && nosync)
    {
      debug ("force && nosync\n");
      exit (0);
    }

  if (!force && ports_count_class (netfs_protid_class) > 0)
    {
      debug ("!force && ports_count_class (netfs_protid_class) > 0\n");
      ports_enable_class (netfs_protid_class);
      ports_resume_class_rpcs (netfs_protid_class);
      devs_unlock ();
      debug ("netfs_S_fsys_goaway return: EBUSY\n");
      return EBUSY;
    }

  if (!nosync)
    {
      debug ("!nosync\n");
      err = netfs_attempt_syncfs (0, flags);
    }

  if (!err && (dev_stop_paging (nosync) || force))
    {
      debug ("!err && (dev_stop_paging (nosync) || force)\n");
      if (!nosync)
        {
          debug ("!nosync\n");
          dev_close (netfs_root_node->nn->dev);
          for (size_t i = 0; i < netfs_root_node->nn->entries_num; ++i)
            dev_close (netfs_root_node->nn->entries[i]->nn->dev);
        }
    }

  if (!err)
    {
      debug ("!err\n");
      fsys_goaway_reply (reply, reply_type, 0);
      debug ("netfs_S_fsys_goaway exit (0)\n");
      exit (0);
    }

  debug ("netfs_S_fsys_goaway return: %d\n", err);
  return err;
}

error_t
netfs_validate_stat (struct node *np, struct iouser *cred)
{
  if (np->nn->dev->store)
    {
      np->nn_stat.st_size = np->nn->dev->store->size;
      np->nn_stat.st_blksize = np->nn->dev->store->block_size;
    }
  else
    {
      np->nn_stat.st_size = 0;
      np->nn_stat.st_blksize = 0;
    }

  return 0;
}

error_t
netfs_attempt_chown (struct iouser *cred, struct node *np, uid_t uid,
                     uid_t gid)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_chauthor (struct iouser *cred, struct node *np, uid_t author)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_chmod (struct iouser *cred, struct node *np, mode_t mode)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_mksymlink (struct iouser *cred, struct node *np,
                         const char *name)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_mkdev (struct iouser *cred, struct node *np, mode_t type,
                     dev_t indexes)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_chflags (struct iouser *cred, struct node *np, int flags)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_utimes (struct iouser *cred, struct node *np,
                      struct timespec *atime, struct timespec *mtime)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_set_size (struct iouser *cred, struct node *np, loff_t size)
{
  return 0;
}

error_t
netfs_attempt_statfs (struct iouser *cred, struct node *np,
                      fsys_statfsbuf_t *st)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_sync (struct iouser *cred, struct node *np, int wait)
{
  error_t err = dev_sync (np->nn->dev, wait);
  if (err)
    {
      debug ("netfs_attempt_sync (cred: %p, np: %p, wait: %d):\n",
             cred, np, wait);
      debug ("netfs_attempt_sync return: %d\n", err);
    }

  return err;
}

error_t
netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  error_t err = dev_sync (netfs_root_node->nn->dev, wait);
  if (err)
    {
      debug ("netfs_attempt_syncfs (cred: %p, wait: %d):\n", cred, wait);
      debug ("dev_sync (netfs_root_node->nn->dev) return err: %d\n", err);
      debug ("netfs_attempt_syncfs return: %d\n", err);
      return err;
    }

  for (size_t i = 0; i < netfs_root_node->nn->entries_num; ++i)
    {
      err = dev_sync (netfs_root_node->nn->entries[i]->nn->dev, wait);
      if (err)
        {
          debug ("netfs_attempt_syncfs (cred: %p, wait: %d):\n", cred, wait);
          debug ("dev_sync (netfs_root_node->nn->entries[%zu]->nn->dev)"
                 " return err: %d\n", i, err);
          debug ("netfs_attempt_syncfs return: %d\n", err);
          break;
        }
    }

  return err;
}

error_t
netfs_attempt_lookup (struct iouser *user, struct node *dir,
                      const char *name, struct node **np)
{
  if (!dir->nn->entries)
    {
      if (dir != netfs_root_node)
        {
          debug ("netfs_attempt_lookup (user: %p, dir: %p, name: %s):\n",
                 user, dir, name);
          debug ("!dir->nn->entries\n");
          debug ("dir != netfs_root_node\n");
          *np = NULL;
          pthread_mutex_unlock (&dir->lock);
          debug ("netfs_attempt_lookup return: ENOTDIR\n");
          return ENOTDIR;
        }

      error_t err = create_partitions ();
      if (err)
        {
          debug ("netfs_attempt_lookup (user: %p, dir: %p, name: %s):\n",
                 user, dir, name);
          debug ("!dir->nn->entries\n");
          debug ("create_partitions return err: %d\n", err);
          *np = NULL;
          pthread_mutex_unlock (&dir->lock);
          debug ("netfs_attempt_lookup return: %d\n", err);
          return err;
        }
    }
  pthread_mutex_unlock (&dir->lock);

  if (*name == '\0' || strcmp (name, ".") == 0)
    {
      *np = dir;
      pthread_mutex_lock (&dir->lock);
      netfs_nref (*np);
      pthread_mutex_unlock (&dir->lock);
      pthread_mutex_lock (&(*np)->lock);
      return 0;
    }

  struct node *current_node = NULL;
  struct node *iter;
  for (size_t i = 0; i < dir->nn->entries_num; ++i)
    {
      iter = dir->nn->entries[i];

      if (strcmp (name, iter->nn->name) == 0)
        {
          current_node = iter;
          break;
        }
    }

  if (current_node)
    {
      *np = current_node;
      pthread_mutex_lock (&dir->lock);
      netfs_nref (*np);
      pthread_mutex_unlock (&dir->lock);
      pthread_mutex_lock (&(*np)->lock);
      return 0;
    }

  *np = NULL;
  debug ("netfs_attempt_lookup (user: %p, dir: %p, name: %s):\n",
          user, dir, name);
  debug ("netfs_attempt_lookup return: ENOENT\n");
  return ENOENT;
}

error_t
netfs_attempt_unlink (struct iouser *user, struct node *dir, const char *name)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_rename (struct iouser *user, struct node *fromdir,
                      const char *fromname, struct node *todir,
                      const char *toname, int excl)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_mkdir (struct iouser *user, struct node *dir, const char *name,
                     mode_t mode)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_rmdir (struct iouser *user, struct node *dir, const char *name)
{
  return EOPNOTSUPP;
}

error_t
netfs_attempt_link (struct iouser *user, struct node *dir, struct node *file,
                    const char *name, int excl)
{
  return EOPNOTSUPP;
}

/* We don't use this function, but we need to unlock the dir.  */
error_t
netfs_attempt_mkfile (struct iouser *user, struct node *dir, mode_t mode,
                      struct node **np)
{
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}

/* We don't use this function, but we need to unlock the dir and clear np.  */
error_t
netfs_attempt_create_file (struct iouser *user, struct node *dir,
                           const char *name, mode_t mode, struct node **np)
{
  *np = NULL;
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}

error_t
netfs_attempt_readlink (struct iouser *user, struct node *np, char *buf)
{
  return EOPNOTSUPP;
}

error_t
netfs_check_open_permissions (struct iouser *user, struct node *np,
                              int flags, int newnode)
{
  error_t err = 0;

  if (!err && flags & O_READ)
    {
      err = fshelp_access (&np->nn_stat, S_IREAD, user);
      if (err)
        {
          debug ("netfs_check_open_permissions (user: %p, np: %p, flags: %d,"
                 " newnode: %d):\n", user, np, flags, newnode);
          debug ("fshelp_access with S_IREAD return err: %d\n", err);
        }
    }

  if (!err && flags & O_WRITE)
    {
      err = fshelp_access (&np->nn_stat, S_IWRITE, user);
      if (err)
        {
          debug ("netfs_check_open_permissions (user: %p, np: %p, flags: %d,"
                 " newnode: %d):\n", user, np, flags, newnode);
          debug ("fshelp_access with S_IWRITE return err: %d\n", err);
        }
    }

  if (!err && flags & O_EXEC)
    {
      err = fshelp_access (&np->nn_stat, S_IEXEC, user);
      if (err)
        {
          debug ("netfs_check_open_permissions (user: %p, np: %p, flags: %d,"
                 " newnode: %d):\n", user, np, flags, newnode);
          debug ("fshelp_access with S_IEXEC return err: %d\n", err);
        }
    }

  if (!err)
    if (!np->nn->dev->store)
      {
        err = check_dev (np, NULL, flags);
        if (err)
          {
            debug ("netfs_check_open_permissions (user: %p, np: %p,"
                   " flags: %d, newnode: %d):\n", user, np, flags, newnode);
            debug ("check_dev return err: %d\n", err);
          }
      }

  if (err)
    debug ("netfs_check_open_permissions return: %d\n", err);

  return err;
}

error_t
netfs_attempt_read (struct iouser *cred, struct node *np, loff_t offset,
                    size_t *len, void *data)
{
  void *our_data = data;
  error_t err = dev_read (np->nn->dev, offset, *len, &our_data, len);
  if (err)
    return err;

  if (our_data != data)
    {
      memcpy (data, our_data, *len);
      munmap (our_data, *len);
    }

  return 0;
}

error_t
netfs_attempt_write (struct iouser *cred, struct node *np, loff_t offset,
                     size_t *len, const void *data)
{
  return dev_write (np->nn->dev, offset, data, *len, len);
}

error_t
netfs_report_access (struct iouser *cred, struct node *np, int *types)
{
  return EOPNOTSUPP;
}

mach_port_t
netfs_get_filemap (struct node *np, vm_prot_t prot)
{
  mach_port_t memobj;
  errno = dev_get_memory_object (np->nn->dev, prot, &memobj);
  if (errno)
    {
      debug ("netfs_get_filemap (np: %p, vm_prot_t: %d):\n", np, prot);
      memobj = MACH_PORT_NULL;
      debug ("dev_get_memory_object return err: %d\n", errno);
    }

  return memobj;
}

struct iouser *
netfs_make_user (uid_t *uids, int nuids, uid_t *gids, int ngids)
{
  return NULL;
}

void
netfs_node_norefs (struct node *np)
{
  return;
}

/* Returned directory entries are aligned to blocks this many bytes long.
   Must be a power of two.  */
#define DIRENT_ALIGN 4
#define DIRENT_NAME_OFFS offsetof (struct dirent, d_name)

/* Length is structure before the name + the name + '\0', all
   padded to a four-byte alignment.  */
#define DIRENT_LEN(name_len) \
  ((DIRENT_NAME_OFFS + (name_len) + 1 + (DIRENT_ALIGN - 1)) \
   & ~(DIRENT_ALIGN - 1))

static inline int
bump_size (size_t *size, int *count, const char *name, const int nentries,
           const vm_size_t buffsize)
{
  if (nentries == -1 || *count < nentries)
    {
      size_t new_size = *size + DIRENT_LEN (strlen (name));
      if (buffsize > 0 && new_size > buffsize)
        return 0;

      *size = new_size;
      *count += 1;
      return 1;
    }

  return 0;
}

static inline int
add_dir_entry (char **data, const char *name, const ino_t fileno,
               const int type, int *count, const int nentries, size_t *size)
{
  if (nentries == -1 || *count < nentries)
    {
      size_t namlen = strlen (name);
      size_t sz = DIRENT_LEN (namlen);

      if (sz > *size)
        return 0;

      *size -= sz;

      struct dirent hdr;
      hdr.d_fileno = fileno;
      hdr.d_reclen = sz;
      hdr.d_type = type;
      hdr.d_namlen = namlen;

      memcpy (*data, &hdr, DIRENT_NAME_OFFS);
      strcpy (*data + DIRENT_NAME_OFFS, name);

      *data += sz;
      *count += 1;

      return 1;
    }

  return 0;
}

error_t
netfs_get_dirents (struct iouser *cred, struct node *dir, int entry,
                   int nentries, char **data, mach_msg_type_number_t *datacnt,
                   vm_size_t bufsize, int *amt)
{
  if (!dir->nn->entries)
    {
      if (dir != netfs_root_node)
        return ENOTDIR;

      error_t err = create_partitions ();
      if (err)
        return err;
    }

  if (dir->nn->entries_num + 2 <= entry)
    {
      *datacnt = 0;
      *amt = 0;
      *data = NULL;
      return 0;
    }

  int count = 0;
  size_t size = 0;

  if (entry == 0)
    bump_size (&size, &count, ".", nentries, bufsize);
  if (entry <= 1)
    bump_size (&size, &count, "..", nentries, bufsize);

  struct node *current_node;
  for (size_t i = 0; i < dir->nn->entries_num; ++i)
    {
      current_node = dir->nn->entries[i];
      if (!bump_size (&size, &count, current_node->nn->name, nentries,
                      bufsize))
        break;
    }

  void *new_data = mmap (0, size, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  if (new_data == MAP_FAILED)
    return errno;

  *data = (char *) new_data;
  *datacnt = size;
  *amt = count;

  count = 0;
  char *ptr_data = *data;

  if (entry == 0)
    add_dir_entry (&ptr_data, ".", dir->nn_stat.st_ino, DT_DIR, &count,
                   nentries, &size);

  if (entry <= 1)
    add_dir_entry (&ptr_data, "..", 2, DT_DIR, &count, nentries, &size);

  int dirent_type;
  for (size_t i = 0; i < dir->nn->entries_num; ++i)
    {
      current_node = dir->nn->entries[i];
      if (current_node->nn->dev->store->block_size == 1)
        dirent_type = DT_CHR;
      else
        dirent_type = DT_BLK;

      if (!add_dir_entry (&ptr_data, current_node->nn->name,
                          current_node->nn_stat.st_ino, dirent_type, &count,
                          nentries, &size))
        break;
    }

  return 0;
}

static inline int
is_privileged (const struct idvec *uids)
{
  return idvec_contains (uids, 0) || idvec_contains (uids, getuid ());
}

error_t
netfs_file_get_storage_info (struct iouser *cred, struct node *np,
                             mach_port_t **ports,
                             mach_msg_type_name_t *ports_type,
                             mach_msg_type_number_t *num_ports,
                             int **ints,
                             mach_msg_type_number_t *num_ints,
                             off_t **offsets,
                             mach_msg_type_number_t *num_offsets,
                             char **data,
                             mach_msg_type_number_t *data_len)
{
  if (!cred)
    return EOPNOTSUPP;

  *ports_type = MACH_MSG_TYPE_COPY_SEND;
  struct dev *dev = np->nn->dev;
  struct store *store = dev->store;
  if (storeio_stat.enforced && !(store->flags & STORE_ENFORCED))
    {
      /* The --enforced switch tells us not to let anyone
         get at the device, no matter how trustable they are.  */
      size_t name_len = (store->name ? strlen (store->name) + 1 : 0);
      *num_ports = 0;
      int i = 0;
      (*ints)[i++] = STORAGE_OTHER;
      (*ints)[i++] = store->flags;
      (*ints)[i++] = store->block_size;
      (*ints)[i++] = 1; /* num_runs */
      (*ints)[i++] = name_len;
      (*ints)[i++] = 0; /* misc_len */
      *num_ints = i;
      i = 0;
      (*offsets)[i++] = 0;
      (*offsets)[i++] = store->size;
      *num_offsets = i;
      if (store->name)
        memcpy (*data, store->name, name_len);
      *data_len = name_len;
      return 0;
    }

  error_t err;
  if (!is_privileged (cred->uids)
      && !store_is_securely_returnable (store, np->nn_stat.st_mode))
    {
      struct store *clone;
      err = store_clone (store, &clone);
      if (err)
        return err;

      err = store_set_flags (clone, STORE_INACTIVE);
      if (err == EINVAL)
        err = EACCES;
      else
        err = store_return (clone, ports, num_ports, ints, num_ints,
                            offsets, num_offsets, data, data_len);

      store_free (clone);
    }
  else
    err = store_return (store, ports, num_ports, ints, num_ints,
                        offsets, num_offsets, data, data_len);

  return err;
}
