/* Stub routines for hostmux

   Copyright (C) 1997 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>
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

#include <hurd/netfs.h>

/* Attempt to turn NODE (user CRED) into a symlink with target NAME. */
error_t
netfs_attempt_mksymlink (struct iouser *cred, struct node *node, char *name)
{
  return EOPNOTSUPP;
}

/* Attempt to turn NODE (user CRED) into a device.  TYPE is either S_IFBLK or
   S_IFCHR. */
error_t
netfs_attempt_mkdev (struct iouser *cred, struct node *node,
		     mode_t type, dev_t indexes)
{
  return EOPNOTSUPP;
}

/* Attempt to set the passive translator record for FILE to ARGZ (of length
   ARGZLEN) for user CRED. */
error_t
netfs_set_translator (struct iouser *cred, struct node *node,
		      char *argz, size_t argzlen)
{
  return EOPNOTSUPP;
}

/* This should attempt a chflags call for the user specified by CRED on node
   NODE, to change the flags to FLAGS. */
error_t
netfs_attempt_chflags (struct iouser *cred, struct node *node, int flags)
{
  return EOPNOTSUPP;
}

/* This should attempt to set the size of the file NODE (for user CRED) to
   SIZE bytes long. */
error_t
netfs_attempt_set_size (struct iouser *cred, struct node *node, off_t size)
{
  return EOPNOTSUPP;
}

/* This should attempt to fetch filesystem status information for the remote
   filesystem, for the user CRED. */
error_t
netfs_attempt_statfs (struct iouser *cred, struct node *node,
		      struct statfs *st)
{
  return EOPNOTSUPP;
}

/* Delete NAME in DIR for USER. */
error_t
netfs_attempt_unlink (struct iouser *user, struct node *dir, char *name)
{
  return EOPNOTSUPP;
}

/* Note that in this one call, neither of the specific nodes are locked. */
error_t
netfs_attempt_rename (struct iouser *user, struct node *fromdir,
		      char *fromname, struct node *todir, 
		      char *toname, int excl)
{
  return EOPNOTSUPP;
}

/* Attempt to create a new directory named NAME in DIR for USER with mode
   MODE.  */
error_t
netfs_attempt_mkdir (struct iouser *user, struct node *dir,
		     char *name, mode_t mode)
{
  return EOPNOTSUPP;
}

/* Attempt to remove directory named NAME in DIR for USER. */
error_t
netfs_attempt_rmdir (struct iouser *user, 
		     struct node *dir, char *name)
{
  return EOPNOTSUPP;
}

/* Create a link in DIR with name NAME to FILE for USER.  Note that neither
   DIR nor FILE are locked.  If EXCL is set, do not delete the target, but
   return EEXIST if NAME is already found in DIR.  */
error_t
netfs_attempt_link (struct iouser *user, struct node *dir,
		    struct node *file, char *name, int excl)
{
  return EOPNOTSUPP;
}

/* Attempt to create an anonymous file related to DIR for USER with MODE.
   Set *NODE to the returned file upon success.  No matter what, unlock DIR. */
error_t
netfs_attempt_mkfile (struct iouser *user, struct node *dir,
		      mode_t mode, struct node **node)
{
  *node = 0;
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}

/* Read from the file NODE for user CRED starting at OFFSET and continuing for
   up to *LEN bytes.  Put the data at DATA.  Set *LEN to the amount
   successfully read upon return.  */
error_t
netfs_attempt_read (struct iouser *cred, struct node *node,
		    off_t offset, size_t *len, void *data)
{
  return EOPNOTSUPP;
}

/* Write to the file NODE for user CRED starting at OFSET and continuing for up
   to *LEN bytes from DATA.  Set *LEN to the amount seccessfully written upon
   return. */
error_t
netfs_attempt_write (struct iouser *cred, struct node *node,
		     off_t offset, size_t *len, void *data)
{
  return EOPNOTSUPP;
}
