/* General fs node functions

   Copyright (C) 1997, 1999, 2007 Free Software Foundation, Inc.

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

#include <fcntl.h>

#include "hostmux.h"

/* Node maintenance.  */

/* Node NP is all done; free all its associated storage. */
void
netfs_node_norefs (struct node *node)
{
  if (node->nn->name)
    /* Remove our name's pointer to us; the name itself will eventually be
       freed by another party.  */
    node->nn->name->node = 0;
  free (node->nn);
  free (node);
}

/* Attempt to create a file named NAME in DIR for USER with MODE.  Set *NODE
   to the new node upon return.  On any error, clear *NODE.  *NODE should be
   locked on success; no matter what, unlock DIR before returning.  */
error_t
netfs_attempt_create_file (struct iouser *user, struct node *dir,
			   char *name, mode_t mode, struct node **node)
{
  *node = 0;
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}

/* Node NODE is being opened by USER, with FLAGS.  NEWNODE is nonzero if we
   just created this node.  Return an error if we should not permit the open
   to complete because of a permission restriction. */
error_t
netfs_check_open_permissions (struct iouser *user, struct node *node,
			      int flags, int newnode)
{
  error_t err = 0;
  if (flags & O_READ)
    err = fshelp_access (&node->nn_stat, S_IREAD, user);
  if (!err && (flags & O_WRITE))
    err = fshelp_access (&node->nn_stat, S_IWRITE, user);
  if (!err && (flags & O_EXEC))
    err = fshelp_access (&node->nn_stat, S_IEXEC, user);
  return err;
}

/* This should attempt a utimes call for the user specified by CRED on node
   NODE, to change the atime to ATIME and the mtime to MTIME. */
error_t
netfs_attempt_utimes (struct iouser *cred, struct node *node,
		      struct timespec *atime, struct timespec *mtime)
{
  error_t err = fshelp_isowner (&node->nn_stat, cred);
  int flags = TOUCH_CTIME;
  
  if (! err)
    {
      if (atime)
	      node->nn_stat.st_atim = *atime;
      
      if (mtime)
	      node->nn_stat.st_mtim = *mtime;

      fshelp_touch (&node->nn_stat, flags, hostmux_maptime);
    }
  return err;
}

/* Return the valid access types (bitwise OR of O_READ, O_WRITE, and O_EXEC)
   in *TYPES for file NODE and user CRED.  */
error_t
netfs_report_access (struct iouser *cred, struct node *node, int *types)
{
  *types = 0;
  if (fshelp_access (&node->nn_stat, S_IREAD, cred) == 0)
    *types |= O_READ;
  if (fshelp_access (&node->nn_stat, S_IWRITE, cred) == 0)
    *types |= O_WRITE;
  if (fshelp_access (&node->nn_stat, S_IEXEC, cred) == 0)
    *types |= O_EXEC;
  return 0;
}

/* Trivial definitions.  */

/* Make sure that NP->nn_stat is filled with current information.  CRED
   identifies the user responsible for the operation.  */
error_t
netfs_validate_stat (struct node *node, struct iouser *cred)
{
  return 0;
}

/* This should sync the file NODE completely to disk, for the user CRED.  If
   WAIT is set, return only after sync is completely finished.  */
error_t
netfs_attempt_sync (struct iouser *cred, struct node *node, int wait)
{
  return 0;
}
