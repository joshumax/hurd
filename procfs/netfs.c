/* Hurd /proc filesystem, interface with libnetfs.
   Copyright (C) 2010 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <hurd/netfs.h>
#include <hurd/fshelp.h>
#include <sys/mman.h>
#include <mach/vm_param.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "procfs.h"

#define PROCFS_SERVER_NAME "procfs"
#define PROCFS_SERVER_VERSION "0.1.0"
#define PROCFS_MAXSYMLINKS 16


/* Interesting libnetfs callback functions. */ 

/* The user must define this variable.  Set this to the name of the
   filesystem server. */
char *netfs_server_name = PROCFS_SERVER_NAME;

/* The user must define this variables.  Set this to be the server
   version number.  */
char *netfs_server_version = PROCFS_SERVER_VERSION;

/* Maximum number of symlinks to follow before returning ELOOP. */
int netfs_maxsymlinks = PROCFS_MAXSYMLINKS;

/* The user must define this function.  Make sure that NP->nn_stat is
   filled with the most current information.  CRED identifies the user
   responsible for the operation. NP is locked.  */
error_t netfs_validate_stat (struct node *np, struct iouser *cred)
{
  char *contents;
  ssize_t contents_len;
  error_t err;

  /* Only symlinks need to have their size filled, before a read is
     attempted.  */
  if (! S_ISLNK (np->nn_stat.st_mode))
    return 0;

  err = procfs_get_contents (np, &contents, &contents_len);
  if (err)
    return err;

  np->nn_stat.st_size = contents_len;
  return 0;
}

/* The user must define this function.  Read from the locked file NP
   for user CRED starting at OFFSET and continuing for up to *LEN
   bytes.  Put the data at DATA.  Set *LEN to the amount successfully
   read upon return.  */
error_t netfs_attempt_read (struct iouser *cred, struct node *np,
			    loff_t offset, size_t *len, void *data)
{
  char *contents;
  ssize_t contents_len;
  error_t err;

  if (offset == 0)
    procfs_refresh (np);

  err = procfs_get_contents (np, &contents, &contents_len);
  if (err)
    return err;

  contents += offset;
  contents_len -= offset;

  if (*len > contents_len)
    *len = contents_len;
  if (*len < 0)
    *len = 0;

  memcpy (data, contents, *len);
  return 0;
}

/* The user must define this function.  Read the contents of locked
   node NP (a symlink), for USER, into BUF.  */
error_t netfs_attempt_readlink (struct iouser *user, struct node *np,
				char *buf)
{
  char *contents;
  ssize_t contents_len;
  error_t err;

  err = procfs_get_contents (np, &contents, &contents_len);
  if (err)
    return err;

  assert_backtrace (contents_len == np->nn_stat.st_size);
  memcpy (buf, contents, contents_len);
  return 0;
}

/* Helper function for netfs_get_dirents() below.  CONTENTS is an argz
   vector of directory entry names, as returned by procfs_get_contents().
   Convert at most NENTRIES of them to dirent structures, put them in
   DATA (if not NULL), write the number of entries processed in *AMT and
   return the required/used space in DATACNT.  */
static int putentries (char *contents, size_t contents_len, int nentries,
		       char *data, mach_msg_type_number_t *datacnt)
{
  int i;

  *datacnt = 0;
  for (i = 0; contents_len && (nentries < 0 || i < nentries); i++)
    {
      int namlen = strlen (contents);
      int reclen = sizeof (struct dirent) + namlen;

      if (data)
        {
	  struct dirent *d = (struct dirent *) (data + *datacnt);
	  d->d_fileno = 42; /* XXX */
	  d->d_namlen = namlen;
	  d->d_reclen = reclen;
	  d->d_type = DT_UNKNOWN;
	  strcpy (d->d_name, contents);
	}

      *datacnt += reclen;
      contents += namlen + 1;
      contents_len -= namlen + 1;
    }

  return i;
}

/* The user must define this function.  Fill the array *DATA of size
   BUFSIZE with up to NENTRIES dirents from DIR (which is locked)
   starting with entry ENTRY for user CRED.  The number of entries in
   the array is stored in *AMT and the number of bytes in *DATACNT.
   If the supplied buffer is not large enough to hold the data, it
   should be grown.  */
error_t netfs_get_dirents (struct iouser *cred, struct node *dir,
                           int entry, int nentries, char **data,
			   mach_msg_type_number_t *datacnt,
			   vm_size_t bufsize, int *amt)
{
  char *contents;
  ssize_t contents_len;
  error_t err;

  if (entry == 0)
    procfs_refresh (dir);
 
  err = procfs_get_contents (dir, &contents, &contents_len);
  if (err)
    return err;

  /* We depend on the fact that CONTENTS is terminated. */
  assert_backtrace (contents_len == 0 || contents[contents_len - 1] == '\0');

  /* Skip to the first requested entry. */
  while (contents_len && entry--)
    {
      int ofs = strlen (contents) + 1;
      contents += ofs;
      contents_len -= ofs;
    }

  /* Allocate a buffer if necessary. */
  putentries (contents, contents_len, nentries, NULL, datacnt);
  if (bufsize < *datacnt)
    {
      char *n = mmap (0, *datacnt, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, 0, 0);
      if (n == MAP_FAILED)
	return ENOMEM;

      *data = n;
    }

  /* Do the actual conversion. */
  *amt = putentries (contents, contents_len, nentries, *data, datacnt);

  return 0;
}

/* The user must define this function.  Lookup NAME in DIR (which is
   locked) for USER; set *NP to the found name upon return.  If the
   name was not found, then return ENOENT.  On any error, clear *NP.
   (*NP, if found, should be locked and a reference to it generated.
   This call should unlock DIR no matter what.)  */
error_t netfs_attempt_lookup (struct iouser *user, struct node *dir,
			      char *name, struct node **np)
{
  error_t err;

  err = procfs_lookup (dir, name, np);
  pthread_mutex_unlock (&dir->lock);

  if (! err)
    pthread_mutex_lock (&(*np)->lock);

  return err;
}

/* The user must define this function.  Node NP has no more references;
   free all its associated storage. */
void netfs_node_norefs (struct node *np)
{
  procfs_cleanup (np);
  free (np);
}

/* The user may define this function (but should define it together
   with netfs_set_translator).  For locked node NODE with S_IPTRANS
   set in its mode, look up the name of its translator.  Store the
   name into newly malloced storage, and return it in *ARGZ; set
   *ARGZ_LEN to the total length.  */
error_t netfs_get_translator (struct node *np, char **argz,
			      size_t *argz_len)
{
  return procfs_get_translator (np, argz, argz_len);
}


/* Libnetfs callbacks managed with libfshelp. */

/* The user must define this function. Locked node NP is being opened
   by USER, with FLAGS.  NEWNODE is nonzero if we just created this
   node.  Return an error if we should not permit the open to complete
   because of a permission restriction.  */
error_t netfs_check_open_permissions (struct iouser *user, struct node *np,
				      int flags, int newnode)
{
  error_t err = 0;
  if (!err && (flags & O_READ))
    err = fshelp_access (&np->nn_stat, S_IREAD, user);
  if (!err && (flags & O_WRITE))
    err = fshelp_access (&np->nn_stat, S_IWRITE, user);
  if (!err && (flags & O_EXEC))
    err = fshelp_access (&np->nn_stat, S_IEXEC, user);
  return err;
}

/* The user must define this function.  Return the valid access
   types (bitwise OR of O_READ, O_WRITE, and O_EXEC) in *TYPES for
   locked file NP and user CRED.  */
error_t netfs_report_access (struct iouser *cred, struct node *np,
			     int *types)
{
  *types = 0;
  if (fshelp_access (&np->nn_stat, S_IREAD, cred) == 0)
    *types |= O_READ;
  if (fshelp_access (&np->nn_stat, S_IWRITE, cred) == 0)
    *types |= O_WRITE;
  if (fshelp_access (&np->nn_stat, S_IEXEC, cred) == 0)
    *types |= O_EXEC;
  return 0;
}


/* Trivial or unsupported libnetfs callbacks. */

/* The user must define this function.  This should attempt a chmod
   call for the user specified by CRED on locked node NP, to change
   the owner to UID and the group to GID.  */
error_t netfs_attempt_chown (struct iouser *cred, struct node *np,
			     uid_t uid, uid_t gid)
{
  return EROFS;
}

/* The user must define this function.  This should attempt a chauthor
   call for the user specified by CRED on locked node NP, thereby
   changing the author to AUTHOR.  */
error_t netfs_attempt_chauthor (struct iouser *cred, struct node *np,
				uid_t author)
{
  return EROFS;
}

/* The user must define this function.  This should attempt a chmod
   call for the user specified by CRED on locked node NODE, to change
   the mode to MODE.  Unlike the normal Unix and Hurd meaning of
   chmod, this function is also used to attempt to change files into
   other types.  If such a transition is attempted which is
   impossible, then return EOPNOTSUPP.  */
error_t netfs_attempt_chmod (struct iouser *cred, struct node *np,
			     mode_t mode)
{
  return EROFS;
}

/* The user must define this function.  Attempt to turn locked node NP
   (user CRED) into a symlink with target NAME.  */
error_t netfs_attempt_mksymlink (struct iouser *cred, struct node *np,
				 char *name)
{
  return EROFS;
}

/* The user must define this function.  Attempt to turn NODE (user
   CRED) into a device.  TYPE is either S_IFBLK or S_IFCHR.  NP is
   locked.  */
error_t netfs_attempt_mkdev (struct iouser *cred, struct node *np,
			     mode_t type, dev_t indexes)
{
  return EROFS;
}

/* The user must define this function.  This should attempt a chflags
   call for the user specified by CRED on locked node NP, to change
   the flags to FLAGS.  */
error_t netfs_attempt_chflags (struct iouser *cred, struct node *np,
			       int flags)
{
  return EROFS;
}

/* The user must define this function.  This should attempt a utimes
   call for the user specified by CRED on locked node NP, to change
   the atime to ATIME and the mtime to MTIME.  If ATIME or MTIME is
   null, then set to the current time.  */
error_t netfs_attempt_utimes (struct iouser *cred, struct node *np,
			      struct timespec *atime, struct timespec *mtime)
{
  return EROFS;
}

/* The user must define this function.  This should attempt to set the
   size of the locked file NP (for user CRED) to SIZE bytes long.  */
error_t netfs_attempt_set_size (struct iouser *cred, struct node *np,
				loff_t size)
{
  return EROFS;
}

/* The user must define this function.  This should attempt to fetch
   filesystem status information for the remote filesystem, for the
   user CRED. NP is locked.  */
error_t netfs_attempt_statfs (struct iouser *cred, struct node *np,
			      fsys_statfsbuf_t *st)
{
  memset (st, 0, sizeof *st);
  st->f_type = FSTYPE_PROC;
  st->f_fsid = getpid ();
  return 0;
}

/* The user must define this function.  This should sync the locked
   file NP completely to disk, for the user CRED.  If WAIT is set,
   return only after the sync is completely finished.  */
error_t netfs_attempt_sync (struct iouser *cred, struct node *np,
			    int wait)
{
  return 0;
}

/* The user must define this function.  This should sync the entire
   remote filesystem.  If WAIT is set, return only after the sync is
   completely finished.  */
error_t netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  return 0;
}

/* The user must define this function.  Delete NAME in DIR (which is
   locked) for USER.  */
error_t netfs_attempt_unlink (struct iouser *user, struct node *dir,
			      char *name)
{
  return EROFS;
}

/* The user must define this function.  Attempt to rename the
   directory FROMDIR to TODIR. Note that neither of the specific nodes
   are locked.  */
error_t netfs_attempt_rename (struct iouser *user, struct node *fromdir,
			      char *fromname, struct node *todir,
			      char *toname, int excl)
{
  return EROFS;
}

/* The user must define this function.  Attempt to create a new
   directory named NAME in DIR (which is locked) for USER with mode
   MODE. */
error_t netfs_attempt_mkdir (struct iouser *user, struct node *dir,
			     char *name, mode_t mode)
{
  return EROFS;
}

/* The user must define this function.  Attempt to remove directory
   named NAME in DIR (which is locked) for USER.  */
error_t netfs_attempt_rmdir (struct iouser *user,
			     struct node *dir, char *name)
{
  return EROFS;
}


/* The user must define this function.  Create a link in DIR with name
   NAME to FILE for USER. Note that neither DIR nor FILE are
   locked. If EXCL is set, do not delete the target.  Return EEXIST if
   NAME is already found in DIR.  */
error_t netfs_attempt_link (struct iouser *user, struct node *dir,
			    struct node *file, char *name, int excl)
{
  return EROFS;
}

/* The user must define this function.  Attempt to create an anonymous
   file related to DIR (which is locked) for USER with MODE.  Set *NP
   to the returned file upon success. No matter what, unlock DIR.  */
error_t netfs_attempt_mkfile (struct iouser *user, struct node *dir,
			      mode_t mode, struct node **np)
{
  return EROFS;
}

/* The user must define this function.  Attempt to create a file named
   NAME in DIR (which is locked) for USER with MODE.  Set *NP to the
   new node upon return.  On any error, clear *NP.  *NP should be
   locked on success; no matter what, unlock DIR before returning.  */
error_t netfs_attempt_create_file (struct iouser *user, struct node *dir,
				   char *name, mode_t mode, struct node **np)
{
  return EROFS;
}

/* The user must define this function.  Write to the locked file NP
   for user CRED starting at OFSET and continuing for up to *LEN bytes
   from DATA.  Set *LEN to the amount successfully written upon
   return.  */
error_t netfs_attempt_write (struct iouser *cred, struct node *np,
			     loff_t offset, size_t *len, void *data)
{
  return EROFS;
}


