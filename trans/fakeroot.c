/* fakeroot -- a translator for faking actions that aren't really permitted
   Copyright (C) 2002, 2003, 2008, 2013 Free Software Foundation, Inc.

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

#include <hurd/netfs.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <argp.h>
#include <error.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>
#include <hurd/ihash.h>
#include <hurd/paths.h>

#include <version.h>

#include "libnetfs/fs_S.h"
#include "libnetfs/io_S.h"
#include "libnetfs/fsys_S.h"
#include "libports/notify_S.h"
#include "libports/interrupt_S.h"

const char *argp_program_version = STANDARD_HURD_VERSION (fakeroot);

char *netfs_server_name = "fakeroot";
char *netfs_server_version = HURD_VERSION;
int netfs_maxsymlinks = 16;	/* arbitrary */

static auth_t fakeroot_auth_port;

struct netnode
{
  struct node *np;		/* our node */
  hurd_ihash_locp_t idport_locp;/* easy removal pointer in idport ihash */
  mach_port_t idport;		/* port from io_identity */
  int openmodes;		/* O_READ | O_WRITE | O_EXEC */
  file_t file;			/* port on real file */

  unsigned int faked;
};

#define FAKE_UID	(1 << 0)
#define FAKE_GID	(1 << 1)
#define FAKE_AUTHOR	(1 << 2)
#define FAKE_MODE	(1 << 3)
#define FAKE_DEFAULT	(1 << 4)

pthread_mutex_t idport_ihash_lock = PTHREAD_MUTEX_INITIALIZER;
struct hurd_ihash idport_ihash
  = HURD_IHASH_INITIALIZER (offsetof (struct netnode, idport_locp));


/* Make a new virtual node.  Always consumes the ports.  If
   successful, NP will be locked.  */
static error_t
new_node (file_t file, mach_port_t idport, int locked, int openmodes,
	  struct node **np)
{
  error_t err;
  struct netnode *nn = calloc (1, sizeof *nn);
  if (nn == 0)
    {
      mach_port_deallocate (mach_task_self (), file);
      if (idport != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), idport);
      if (locked)
	pthread_mutex_unlock (&idport_ihash_lock);
      return ENOMEM;
    }
  nn->file = file;
  nn->openmodes = openmodes;
  if (idport != MACH_PORT_NULL)
    nn->idport = idport;
  else
    {
      ino_t fileno;
      mach_port_t fsidport;
      assert (!locked);
      err = io_identity (file, &nn->idport, &fsidport, &fileno);
      if (err)
	{
	  mach_port_deallocate (mach_task_self (), file);
	  free (nn);
	  return err;
	}
    }

  if (!locked)
    pthread_mutex_lock (&idport_ihash_lock);
  err = hurd_ihash_add (&idport_ihash, nn->idport, nn);
  if (err)
    goto lose;

  *np = nn->np = netfs_make_node (nn);
  if (*np == 0)
    {
      err = ENOMEM;
      goto lose_hash;
    }

  pthread_mutex_lock (&(*np)->lock);
  pthread_mutex_unlock (&idport_ihash_lock);
  return 0;

 lose_hash:
  hurd_ihash_locp_remove (&idport_ihash, nn->idport_locp);
 lose:
  pthread_mutex_unlock (&idport_ihash_lock);
  mach_port_deallocate (mach_task_self (), nn->idport);
  mach_port_deallocate (mach_task_self (), file);
  free (nn);
  return err;
}

static void
set_default_attributes (struct node *np)
{
  np->nn->faked = FAKE_UID | FAKE_GID | FAKE_DEFAULT;
  np->nn_stat.st_uid = 0;
  np->nn_stat.st_gid = 0;
}

static void
set_faked_attribute (struct node *np, unsigned int faked)
{
  np->nn->faked |= faked;

  if (np->nn->faked & FAKE_DEFAULT)
    {
      /* Now that the node has non-default faked attributes, they have to be
	 retained for future accesses.  Account for the hash table reference.

	 XXX This means such nodes are currently leaked.  Hopefully, there
	 won't be too many of them until the translator is shut down, and
	 the data structures should make implementing garbage collection
	 easy enough if it's ever needed, although scalability could be
	 improved.  */
      netfs_nref (np);
      np->nn->faked &= ~FAKE_DEFAULT;
    }
}

/* Node NP has no more references; free all its associated storage. */
void
netfs_node_norefs (struct node *np)
{
  assert (np->nn->np == np);

  pthread_mutex_unlock (&np->lock);
  pthread_spin_unlock (&netfs_node_refcnt_lock);

  pthread_mutex_lock (&idport_ihash_lock);
  hurd_ihash_locp_remove (&idport_ihash, np->nn->idport_locp);
  pthread_mutex_unlock (&idport_ihash_lock);

  mach_port_deallocate (mach_task_self (), np->nn->file);
  mach_port_deallocate (mach_task_self (), np->nn->idport);
  free (np->nn);
  free (np);

  pthread_spin_lock (&netfs_node_refcnt_lock);
}

/* This is the cleanup function we install in netfs_protid_class.  If
   the associated nodes reference count would also drop to zero, and
   the node has no faked attributes, we destroy it as well.  */
static void
fakeroot_netfs_release_protid (void *cookie)
{
  netfs_release_protid (cookie);

  int cports = ports_count_class (netfs_control_class);
  int nports = ports_count_class (netfs_protid_class);
  ports_enable_class (netfs_control_class);
  ports_enable_class (netfs_protid_class);
  if (cports == 0 && nports == 0)
    {
      /* The last client is gone.  Our job is done.  */
      error_t err = netfs_shutdown (0);
      if (! err)
        exit (EXIT_SUCCESS);

      /* If netfs_shutdown returns EBUSY, we lost a race against
         fsys_goaway.  Hence we ignore this error.  */
      if (err != EBUSY)
        error (1, err, "netfs_shutdown");
    }
}

/* Given an existing node, make sure it has NEWMODES in its openmodes.
   If not null, FILE is a port with those openmodes.  */
static error_t
check_openmodes (struct netnode *nn, int newmodes, file_t file)
{
  error_t err = 0;

  if (newmodes &~ nn->openmodes)
    {
      /* The user wants openmodes we haven't tried before.  */

      if (file != MACH_PORT_NULL && (nn->openmodes & ~newmodes))
	{
	  /* Intersecting sets.
	     We need yet another new peropen on this node.  */
	  mach_port_deallocate (mach_task_self (), file);
	  file = MACH_PORT_NULL;
	}
      if (file == MACH_PORT_NULL)
	{
	  enum retry_type bad_retry;
	  char bad_retryname[1024];	/* XXX */
	  err = dir_lookup (nn->file, "", nn->openmodes | newmodes, 0,
			    &bad_retry, bad_retryname, &file);
	  if (!err && (bad_retry != FS_RETRY_NORMAL
		       || bad_retryname[0] != '\0'))
	    {
	      mach_port_deallocate (mach_task_self (), file);
	      err = EGRATUITOUS;
	    }
	}
      if (! err)
	{
	  /* The new port has more openmodes than
	     the old one.  We can just use it now.  */
	  mach_port_deallocate (mach_task_self (), nn->file);
	  nn->file = file;
	  nn->openmodes = newmodes;
	}
    }
  else if (file != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), file);

  return err;
}

/* This is called by netfs_S_fsys_getroot.  */
error_t
netfs_check_open_permissions (struct iouser *user, struct node *np,
			      int flags, int newnode)
{
  return check_openmodes (np->nn, flags & (O_RDWR|O_EXEC), MACH_PORT_NULL);
}

error_t
netfs_S_dir_lookup (struct protid *diruser,
		    char *filename,
		    int flags,
		    mode_t mode,
		    retry_type *do_retry,
		    char *retry_name,
		    mach_port_t *retry_port,
		    mach_msg_type_name_t *retry_port_type)
{
  struct node *dnp, *np;
  error_t err;
  struct protid *newpi;
  struct iouser *user;
  mach_port_t file;
  mach_port_t idport, fsidport;
  ino_t fileno;

  if (!diruser)
    return EOPNOTSUPP;

  dnp = diruser->po->np;

  mach_port_t dir = dnp->nn->file;
 redo_lookup:
  err = dir_lookup (dir, filename,
		    flags & (O_NOLINK|O_RDWR|O_EXEC|O_CREAT|O_EXCL|O_NONBLOCK),
		    mode, do_retry, retry_name, &file);
  if (dir != dnp->nn->file)
    mach_port_deallocate (mach_task_self (), dir);
  if (err)
    return err;

  switch (*do_retry)
    {
    case FS_RETRY_REAUTH:
      {
	mach_port_t ref = mach_reply_port ();
	err = io_reauthenticate (file, ref, MACH_MSG_TYPE_MAKE_SEND);
	if (! err)
	  {
	    mach_port_deallocate (mach_task_self (), file);
	    err = auth_user_authenticate (fakeroot_auth_port, ref,
					  MACH_MSG_TYPE_MAKE_SEND,
					  &dir);
	  }
	mach_port_destroy (mach_task_self (), ref);
	if (err)
	  return err;
      }
      filename = retry_name;
      goto redo_lookup;

    case FS_RETRY_NORMAL:
      if (retry_name[0] != '\0')
	{
	  dir = file;
	  filename = retry_name;
	  goto redo_lookup;
	}
      break;

    case FS_RETRY_MAGICAL:
      if (file == MACH_PORT_NULL)
	{
	  *retry_port = MACH_PORT_NULL;
	  *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
	  return 0;
	}
      /* Fallthrough.  */

    default:
      /* Invalid response to our dir_lookup request.  */
      if (file != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), file);
      *retry_port = MACH_PORT_NULL;
      *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
      return EOPNOTSUPP;
    }

  /* We have a new port to an underlying node.
     Find or make our corresponding virtual node.  */

  np = 0;
  err = io_identity (file, &idport, &fsidport, &fileno);
  if (err)
    {
      mach_port_deallocate (mach_task_self (), file);
      return err;
    }

  mach_port_deallocate (mach_task_self (), fsidport);

 redo_hash_lookup:
  pthread_mutex_lock (&idport_ihash_lock);
  pthread_mutex_lock (&dnp->lock);
  /* The hashtable may not hold a true reference on the node.  Acquire the
     refcount lock so that, if a node is found, its reference counter cannot
     drop to 0 before we get our own reference.  */
  pthread_spin_lock (&netfs_node_refcnt_lock);
  struct netnode *nn = hurd_ihash_find (&idport_ihash, idport);
  if (nn != NULL)
    {
      assert (nn->np->nn == nn);
      /* We already know about this node.  */

      if (nn->np->references == 0)
	{
	  /* But it might be in the process of being released.  If so,
	     unlock the hash table to give the node a chance to actually
	     be removed and retry.  */
	  pthread_spin_unlock (&netfs_node_refcnt_lock);
	  pthread_mutex_unlock (&dnp->lock);
	  pthread_mutex_unlock (&idport_ihash_lock);
	  goto redo_hash_lookup;
	}

      /* Otherwise, reference it right away.  */
      np = nn->np;
      np->references++;
      pthread_spin_unlock (&netfs_node_refcnt_lock);

      mach_port_deallocate (mach_task_self (), idport);

      if (np == dnp)
	{
	  /* dnp is already locked.  */
	}
      else
	{
	  pthread_mutex_lock (&np->lock);
	  pthread_mutex_unlock (&dnp->lock);
	}

      err = check_openmodes (np->nn, (flags & (O_RDWR|O_EXEC)), file);
      pthread_mutex_unlock (&idport_ihash_lock);
    }
  else
    {
      pthread_spin_unlock (&netfs_node_refcnt_lock);
      err = new_node (file, idport, 1, flags, &np);
      pthread_mutex_unlock (&dnp->lock);
      if (!err)
	{
	  set_default_attributes (np);
	  err = netfs_validate_stat (np, diruser->user);
	}
    }
  if (err)
    goto lose;

  assert (retry_name[0] == '\0' && *do_retry == FS_RETRY_NORMAL);
  flags &= ~(O_CREAT|O_EXCL|O_NOLINK|O_NOTRANS|O_NONBLOCK);

  err = iohelp_dup_iouser (&user, diruser->user);
  if (!err)
    {
      newpi = netfs_make_protid (netfs_make_peropen (np, flags, diruser->po),
				 user);
      if (! newpi)
	{
	  iohelp_free_iouser (user);
	  err = errno;
	}
      else
	{
	  *retry_port = ports_get_right (newpi);
	  *retry_port_type = MACH_MSG_TYPE_MAKE_SEND;
	  ports_port_deref (newpi);
	}
    }

 lose:
  if (np != NULL)
    netfs_nput (np);
  return err;
}

/* These callbacks are used only by the standard netfs_S_dir_lookup,
   which we do not use.  But the shared library requires us to define them.  */
error_t
netfs_attempt_lookup (struct iouser *user, struct node *dir,
		      char *name, struct node **np)
{
  assert (! "should not be here");
  return EIEIO;
}

error_t
netfs_attempt_create_file (struct iouser *user, struct node *dir,
			   char *name, mode_t mode, struct node **np)
{
  assert (! "should not be here");
  return EIEIO;
}

/* Make sure that NP->nn_stat is filled with the most current information.
   CRED identifies the user responsible for the operation. NP is locked.  */
error_t
netfs_validate_stat (struct node *np, struct iouser *cred)
{
  struct stat st;
  error_t err = io_stat (np->nn->file, &st);
  if (err)
    return err;

  if (np->nn->faked & FAKE_UID)
    st.st_uid = np->nn_stat.st_uid;
  if (np->nn->faked & FAKE_GID)
    st.st_gid = np->nn_stat.st_gid;
  if (np->nn->faked & FAKE_AUTHOR)
    st.st_author = np->nn_stat.st_author;
  if (np->nn->faked & FAKE_MODE)
    st.st_mode = np->nn_stat.st_mode;

  np->nn_stat = st;
  np->nn_translated = S_ISLNK (st.st_mode) ? S_IFLNK : 0;

  return 0;
}

error_t
netfs_attempt_chown (struct iouser *cred, struct node *np,
		     uid_t uid, uid_t gid)
{
  if (uid != ~0U)
    {
      set_faked_attribute (np, FAKE_UID);
      np->nn_stat.st_uid = uid;
    }
  if (gid != ~0U)
    {
      set_faked_attribute (np, FAKE_GID);
      np->nn_stat.st_gid = gid;
    }
  return 0;
}

error_t
netfs_attempt_chauthor (struct iouser *cred, struct node *np, uid_t author)
{
  set_faked_attribute (np, FAKE_AUTHOR);
  np->nn_stat.st_author = author;
  return 0;
}

/* Return the mode that the real underlying file should have if the
   fake mode is being set to MODE.  We always give ourselves read and
   write permission so that we can open the file as root would be able
   to.  We give ourselves execute permission iff any execute bit is
   set in the fake mode.  */
static inline mode_t
real_from_fake_mode (mode_t mode)
{
  return mode | S_IREAD | S_IWRITE | (((mode << 3) | (mode << 6)) & S_IEXEC);
}

/* This should attempt a chmod call for the user specified by CRED on
   locked node NODE, to change the mode to MODE.  Unlike the normal Unix
   and Hurd meaning of chmod, this function is also used to attempt to
   change files into other types.  If such a transition is attempted which
   is impossible, then return EOPNOTSUPP.  */
error_t
netfs_attempt_chmod (struct iouser *cred, struct node *np, mode_t mode)
{
  if ((mode & S_IFMT) == 0)
    mode |= np->nn_stat.st_mode & S_IFMT;
  if ((mode & S_IFMT) != (np->nn_stat.st_mode & S_IFMT))
    return EOPNOTSUPP;

  /* We don't bother with error checking since the fake mode change should
     always succeed--worst case a later open will get EACCES.  */
  (void) file_chmod (np->nn->file, mode);
  set_faked_attribute (np, FAKE_MODE);
  np->nn_stat.st_mode = mode;
  return 0;
}

/* The user must define this function.  Attempt to turn locked node NP
   (user CRED) into a symlink with target NAME.  */
error_t
netfs_attempt_mksymlink (struct iouser *cred, struct node *np, char *name)
{
  int namelen = strlen (name) + 1;
  char trans[sizeof _HURD_SYMLINK + namelen];
  memcpy (trans, _HURD_SYMLINK, sizeof _HURD_SYMLINK);
  memcpy (&trans[sizeof _HURD_SYMLINK], name, namelen);
  return file_set_translator (np->nn->file,
			      FS_TRANS_EXCL|FS_TRANS_SET,
			      FS_TRANS_EXCL|FS_TRANS_SET, 0,
			      trans, sizeof trans,
			      MACH_PORT_NULL, MACH_MSG_TYPE_COPY_SEND);
}

error_t
netfs_attempt_mkdev (struct iouser *cred, struct node *np,
		     mode_t type, dev_t indexes)
{
  char *trans = 0;
  int translen = asprintf (&trans, "%s%c%d%c%d",
			   S_ISCHR (type) ? _HURD_CHRDEV : _HURD_BLKDEV,
			   '\0', major (indexes), '\0', minor (indexes));
  if (trans == 0)
    return ENOMEM;
  else
    {
      error_t err = file_set_translator (np->nn->file,
					 FS_TRANS_EXCL|FS_TRANS_SET,
					 FS_TRANS_EXCL|FS_TRANS_SET, 0,
					 trans, translen + 1,
					 MACH_PORT_NULL,
					 MACH_MSG_TYPE_COPY_SEND);
      free (trans);
      return err;
    }
}

error_t
netfs_attempt_chflags (struct iouser *cred, struct node *np, int flags)
{
  return file_chflags (np->nn->file, flags);
}

error_t
netfs_attempt_utimes (struct iouser *cred, struct node *np,
		      struct timespec *atime, struct timespec *mtime)
{
  union tv
  {
    struct timeval tv;
    time_value_t tvt;
  };
  union tv a, m;
  if (atime)
    {
      TIMESPEC_TO_TIMEVAL (&a.tv, atime);
    }
  else
    a.tv.tv_sec = a.tv.tv_usec = -1;
  if (mtime)
    {
      TIMESPEC_TO_TIMEVAL (&m.tv, mtime);
    }
  else
    m.tv.tv_sec = m.tv.tv_usec = -1;

  return file_utimes (np->nn->file, a.tvt, m.tvt);
}

error_t
netfs_attempt_set_size (struct iouser *cred, struct node *np, off_t size)
{
  return file_set_size (np->nn->file, size);
}

error_t
netfs_attempt_statfs (struct iouser *cred, struct node *np, struct statfs *st)
{
  return file_statfs (np->nn->file, st);
}

error_t
netfs_attempt_sync (struct iouser *cred, struct node *np, int wait)
{
  return file_sync (np->nn->file, wait, 0);
}

error_t
netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  return 0;
}

error_t
netfs_attempt_mkdir (struct iouser *user, struct node *dir,
		     char *name, mode_t mode)
{
  return dir_mkdir (dir->nn->file, name, mode | S_IRWXU);
}


/* XXX
   Removing a node should mark the netnode so that it is GC'd when
   it has no hard refs.
 */

error_t
netfs_attempt_unlink (struct iouser *user, struct node *dir, char *name)
{
  return dir_unlink (dir->nn->file, name);
}

error_t
netfs_attempt_rename (struct iouser *user, struct node *fromdir,
		      char *fromname, struct node *todir,
		      char *toname, int excl)
{
  return dir_rename (fromdir->nn->file, fromname,
		     todir->nn->file, toname, excl);
}

error_t
netfs_attempt_rmdir (struct iouser *user,
		     struct node *dir, char *name)
{
  return dir_rmdir (dir->nn->file, name);
}

error_t
netfs_attempt_link (struct iouser *user, struct node *dir,
		    struct node *file, char *name, int excl)
{
  return dir_link (dir->nn->file, file->nn->file, name, excl);
}

error_t
netfs_attempt_mkfile (struct iouser *user, struct node *dir,
		      mode_t mode, struct node **np)
{
  file_t newfile;
  error_t err = dir_mkfile (dir->nn->file, O_RDWR|O_EXEC,
			    real_from_fake_mode (mode), &newfile);
  pthread_mutex_unlock (&dir->lock);
  if (err == 0)
    err = new_node (newfile, MACH_PORT_NULL, 0, O_RDWR|O_EXEC, np);
  if (err == 0)
    pthread_mutex_unlock (&(*np)->lock);
  return err;
}

error_t
netfs_attempt_readlink (struct iouser *user, struct node *np, char *buf)
{
  char transbuf[sizeof _HURD_SYMLINK + np->nn_stat.st_size + 1];
  char *trans = transbuf;
  size_t translen = sizeof transbuf;
  error_t err = file_get_translator (np->nn->file, &trans, &translen);
  if (err == 0)
    {
      if (translen < sizeof _HURD_SYMLINK
	  || memcmp (trans, _HURD_SYMLINK, sizeof _HURD_SYMLINK) != 0)
	err = EINVAL;
      else
	{
	  assert (translen <= sizeof _HURD_SYMLINK + np->nn_stat.st_size + 1);
	  memcpy (buf, &trans[sizeof _HURD_SYMLINK],
		  translen - sizeof _HURD_SYMLINK);
	}
      if (trans != transbuf)
	munmap (trans, translen);
    }
  return err;
}

error_t
netfs_attempt_read (struct iouser *cred, struct node *np,
		    off_t offset, size_t *len, void *data)
{
  char *buf = data;
  error_t err = io_read (np->nn->file, &buf, len, offset, *len);
  if (err == 0 && buf != data)
    {
      memcpy (data, buf, *len);
      munmap (buf, *len);
    }
  return err;
}

error_t
netfs_attempt_write (struct iouser *cred, struct node *np,
		     off_t offset, size_t *len, void *data)
{
  return io_write (np->nn->file, data, *len, offset, len);
}

error_t
netfs_report_access (struct iouser *cred, struct node *np, int *types)
{
  *types = O_RDWR|O_EXEC;
  return 0;
}

error_t
netfs_get_dirents (struct iouser *cred, struct node *dir,
		   int entry, int nentries, char **data,
		   mach_msg_type_number_t *datacnt,
		   vm_size_t bufsize, int *amt)
{
  return dir_readdir (dir->nn->file, data, datacnt,
		      entry, nentries, bufsize, amt);
}

error_t
netfs_file_get_storage_info (struct iouser *cred,
			     struct node *np,
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
  *ports_type = MACH_MSG_TYPE_MOVE_SEND;
  return file_get_storage_info (np->nn->file,
				ports, num_ports,
				ints, num_ints,
				offsets, num_offsets,
				data, data_len);
}

kern_return_t
netfs_S_file_exec (struct protid *user,
                   task_t task,
                   int flags,
                   char *argv,
                   size_t argvlen,
                   char *envp,
                   size_t envplen,
                   mach_port_t *fds,
                   size_t fdslen,
                   mach_port_t *portarray,
                   size_t portarraylen,
                   int *intarray,
                   size_t intarraylen,
                   mach_port_t *deallocnames,
                   size_t deallocnameslen,
                   mach_port_t *destroynames,
                   size_t destroynameslen)
{
  error_t err;
  file_t file;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&user->po->np->lock);
  err = check_openmodes (user->po->np->nn, O_EXEC, MACH_PORT_NULL);
  file = user->po->np->nn->file;
  if (!err)
    err = mach_port_mod_refs (mach_task_self (),
			      file, MACH_PORT_RIGHT_SEND, 1);
  pthread_mutex_unlock (&user->po->np->lock);

  if (!err)
    {
      /* We cannot use MACH_MSG_TYPE_MOVE_SEND because we might need to
	 retry an interrupted call that would have consumed the rights.  */
      err = file_exec (user->po->np->nn->file, task, flags, argv, argvlen,
		       envp, envplen, fds, MACH_MSG_TYPE_COPY_SEND, fdslen,
		       portarray, MACH_MSG_TYPE_COPY_SEND, portarraylen,
		       intarray, intarraylen, deallocnames, deallocnameslen,
		       destroynames, destroynameslen);
      mach_port_deallocate (mach_task_self (), file);
    }

  if (err == 0)
    {
      size_t i;
      mach_port_deallocate (mach_task_self (), task);
      for (i = 0; i < fdslen; ++i)
	mach_port_deallocate (mach_task_self (), fds[i]);
      for (i = 0; i < portarraylen; ++i)
	mach_port_deallocate (mach_task_self (), portarray[i]);
    }
  return err;
}

error_t
netfs_S_io_map (struct protid *user,
		mach_port_t *rdobj, mach_msg_type_name_t *rdobjtype,
		mach_port_t *wrobj, mach_msg_type_name_t *wrobjtype)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;
  *rdobjtype = *wrobjtype = MACH_MSG_TYPE_MOVE_SEND;

  pthread_mutex_lock (&user->po->np->lock);
  err = io_map (user->po->np->nn->file, rdobj, wrobj);
  pthread_mutex_unlock (&user->po->np->lock);
  return err;
}

error_t
netfs_S_io_map_cntl (struct protid *user,
                     mach_port_t *obj,
                     mach_msg_type_name_t *objtype)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;
  *objtype = MACH_MSG_TYPE_MOVE_SEND;

  pthread_mutex_lock (&user->po->np->lock);
  err = io_map_cntl (user->po->np->nn->file, obj);
  pthread_mutex_unlock (&user->po->np->lock);
  return err;
}

error_t
netfs_S_io_identity (struct protid *user,
		     mach_port_t *id,
		     mach_msg_type_name_t *idtype,
		     mach_port_t *fsys,
		     mach_msg_type_name_t *fsystype,
		     ino_t *fileno)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  *idtype = *fsystype = MACH_MSG_TYPE_MOVE_SEND;

  pthread_mutex_lock (&user->po->np->lock);
  err = io_identity (user->po->np->nn->file, id, fsys, fileno);
  pthread_mutex_unlock (&user->po->np->lock);
  return err;
}

#define NETFS_S_SIMPLE(name)			\
error_t						\
netfs_S_##name (struct protid *user)		\
{						\
  error_t err;					\
						\
  if (!user)					\
    return EOPNOTSUPP;				\
						\
  pthread_mutex_lock (&user->po->np->lock);	\
  err = name (user->po->np->nn->file);		\
  pthread_mutex_unlock (&user->po->np->lock);	\
  return err;					\
}

NETFS_S_SIMPLE (io_get_conch)
NETFS_S_SIMPLE (io_release_conch)
NETFS_S_SIMPLE (io_eofnotify)
NETFS_S_SIMPLE (io_readnotify)
NETFS_S_SIMPLE (io_readsleep)
NETFS_S_SIMPLE (io_sigio)

error_t
netfs_S_io_prenotify (struct protid *user,
                      vm_offset_t start, vm_offset_t stop)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&user->po->np->lock);
  err = io_prenotify (user->po->np->nn->file, start, stop);
  pthread_mutex_unlock (&user->po->np->lock);
  return err;
}

error_t
netfs_S_io_postnotify (struct protid *user,
                       vm_offset_t start, vm_offset_t stop)
{
  error_t err;

  if (!user)
    return EOPNOTSUPP;

  pthread_mutex_lock (&user->po->np->lock);
  err = io_postnotify (user->po->np->nn->file, start, stop);
  pthread_mutex_unlock (&user->po->np->lock);
  return err;
}

/* This overrides the library's definition.  */
int
netfs_demuxer (mach_msg_header_t *inp,
	       mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = netfs_io_server_routine (inp)) ||
      (routine = netfs_fs_server_routine (inp)) ||
      (routine = ports_notify_server_routine (inp)) ||
      (routine = netfs_fsys_server_routine (inp)) ||
      /* XXX we should intercept interrupt_operation and do
	 the ports_S_interrupt_operation work as well as
	 sending an interrupt_operation to the underlying file.
       */
      (routine = ports_interrupt_server_routine (inp)))
    {
      (*routine) (inp, outp);
      return TRUE;
    }
  else
    {
      /* We didn't recognize the message ID, so pass the message through
	 unchanged to the underlying file.  */
      struct protid *cred = ports_lookup_port (netfs_port_bucket,
					       inp->msgh_local_port,
					       netfs_protid_class);
      if (cred == 0)
	/* This must be an unknown message on our fsys control port.  */
	return 0;
      else
	{
	  error_t err;
	  assert (MACH_MSGH_BITS_LOCAL (inp->msgh_bits)
		  == MACH_MSG_TYPE_MOVE_SEND);
	  inp->msgh_bits = (inp->msgh_bits & MACH_MSGH_BITS_COMPLEX)
	    | MACH_MSGH_BITS (MACH_MSG_TYPE_COPY_SEND,
			      MACH_MSGH_BITS_REMOTE (inp->msgh_bits));
	  inp->msgh_local_port = inp->msgh_remote_port;	/* reply port */
	  inp->msgh_remote_port = cred->po->np->nn->file;
	  err = mach_msg (inp, MACH_SEND_MSG, inp->msgh_size, 0,
			  MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE,
			  MACH_PORT_NULL);
	  assert_perror (err);	/* XXX should synthesize reply */
	  ports_port_deref (cred);
	  /* We already sent the message, so the server loop shouldn't do it again.  */
	  ((mig_reply_header_t *) outp)->RetCode = MIG_NO_REPLY;
	  return 1;
	}
    }
}


int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;

  struct argp argp = { NULL, NULL, NULL, "\
A translator for faking privileged access to an underlying filesystem.\v\
This translator appears to give transparent access to the underlying \
directory node.  However, all accesses are made using the credentials \
of the translator regardless of the client and the translator fakes \
success for chown and chmod operations that only root could actually do, \
reporting the faked IDs and modes in later stat calls, and allows \
any user to open nodes regardless of permissions as is done for root." };

  /* Parse our command line arguments (all none of them).  */
  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  fakeroot_auth_port = getauth ();

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  netfs_init ();

  /* Install our own clean routine.  */
  netfs_protid_class->clean_routine = fakeroot_netfs_release_protid;

  /* Get our underlying node (we presume it's a directory) and use
     that to make the root node of the filesystem.  */
  err = new_node (netfs_startup (bootstrap, O_READ), MACH_PORT_NULL, 0, O_READ,
		  &netfs_root_node);
  if (err)
    error (5, err, "Cannot create root node");

  err = netfs_validate_stat (netfs_root_node, 0);
  if (err)
    error (6, err, "Cannot stat underlying node");

  netfs_root_node->nn_stat.st_mode &= ~(S_IPTRANS | S_IATRANS);
  netfs_root_node->nn_stat.st_mode |= S_IROOT;
  set_faked_attribute (netfs_root_node, FAKE_MODE);
  pthread_mutex_unlock (&netfs_root_node->lock);

  netfs_server_loop ();		/* Never returns.  */

  /*NOTREACHED*/
  return 0;
}
