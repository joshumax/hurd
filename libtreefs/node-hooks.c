/* Default hooks for nodes

   Copyright (C) 1995 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "treefs.h"

/* ---------------------------------------------------------------- */
/* These default hooks depend on stat information being correct.  */

/* Returns the type of NODE, as an S_IFMT value (e.g., S_IFDIR).  The
   default routine just looks at NODE's stat mode.  */
int
_treefs_node_type (struct treefs_node *node)
{
  return node->stat.st_mode & S_IFMT;
}

/* Return TRUE if NODE is `unlinked' -- that is, can be deleted when all
   (in-memory) references go away.  */
int
_treefs_node_unlinked (struct treefs_node *node)
{
  return node->stat.st_nlinks == 0;
}

/* Changes the link count of NODE by CHANGE; if any error is returned, the
   operation trying to change the link count will fail, so filesystems that
   don't support real links can restrict it to 1 or 0.  This is mostly used
   by the in-core directory code when it makes a link.  The default hook uses
   the link field of NODE's stat entry.  */
error_t
_treefs_node_mod_link_count (struct treefs_node *node, int change)
{
  node->stat.st_nlinks += change;
}


/* ---------------------------------------------------------------- */
/* These default hooks depend on stat information being correct.  */

/* Returns the user and group that a newly started translator should be
   authenticated as.  The default just returns the owner/group of NODE.  */
error_t
_treefs_node_get_trans_auth (struct treefs_node *node, uid_t *uid, gid_t *gid)
{
  *uid = node->stat.st_uid;
  *gid = node->stat.st_gid;
  return 0;
}

/* Check to see is the user identified by AUTH is permitted to do 
   operation OP on node NP.  Op is one of S_IREAD, S_IWRITE, or S_IEXEC.
   Return 0 if the operation is permitted and EACCES if not. */
error_t 
_treefs_node_access (struct treefs_node *node,
		     int op, struct treefs_auth *auth)
{
  int gotit;
  if (diskfs_auth_has_uid (auth, 0))
    gotit = 1;
  else if (auth->nuids == 0 && (node->stat.st_mode & S_IUSEUNK))
    gotit = node->stat.st_mode & (op << S_IUNKSHIFT);
  else if (!treefs_node_owned (node, auth))
    gotit = node->stat.st_mode & op;
  else if (treefs_auth_in_group (auth, node->stat.st_gid))
    gotit = node->stat.st_mode & (op >> 3);
  else 
    gotit = node->stat.st_mode & (op >> 6);
  return gotit ? 0 : EACCES;
}

/* Check to see if the user identified by AUTH is permitted to do owner-only
   operations on node NP; if so, return 0; if not, return EPERM. */
error_t
_treefs_node_owned (struct treefs_node *node, struct treefs_auth *auth)
{
  /* Permitted if the user is the owner, superuser, or if the user
     is in the group of the file and has the group ID as their user
     ID.  (This last is colloquially known as `group leader'.) */
  if (treefs_auth_has_uid (auth, node->stat.st_uid)
      || treefs_auth_has_uid (auth, 0)
      || (treefs_auth_in_group (auth, node->stat.st_gid)
	  && treefs_auth_has_uid (auth, node->stat.st_gid)))
    return 0;
  else
    return EPERM;
}

/* ---------------------------------------------------------------- */

error_t
_treefs_node_init_stat (struct treefs_node *node, struct treefs_node *dir,
			mode_t mode, struct treefs_auth *auth)
{
  if (auth->nuids)
    node->stat.st_uid = auth->uids[0];
  else
    {
      mode &= ~S_ISUID;
      if (dir)
	node->stat.st_uid = dir->stat.st_uid;
      else
	node->stat.st_uid = -1;	/* XXX */
    }

  if (dir && diskfs_ingroup (dir->stat.st_gid, auth))
    node->stat.st_gid = dir->stat.st_gid;
  else if (auth->ngids)
    node->stat.st_gid = auth->gids[0];
  else
    {
      mode &= ~S_ISGID;
      if (dir)
	node->stat.st_gid = dir->stat.st_gid;
      else
	node->stat.st_gid = -1; /* XXX */
    }
  
  node->stat.st_rdev = 0;
  node->stat.st_nlink = 0;
  node->stat.st_mode = mode;

  node->stat.st_blocks = 0;
  node->stat.st_size = 0;
  node->stat.st_flags = 0;

  return 0;
}

/* ---------------------------------------------------------------- */

/* Called when the new peropen structure PO is made for NODE, with the
   authorization in AUTH, opened with the flags FLAGS.  If an error is
   returned, the open will fail with that error.  The default hook does
   explicit authorization checks against AUTH using treefs_node_access, and
   otherwise does nothing.  */
error_t
_treefs_init_peropen (struct treefs_node *node, struct treefs_peropen *po,
		      int flags, struct treefs_auth *auth)
{
  error_t err;

  if (flags & O_READ)
    err = treefs_node_access (node, S_IREAD, auth);
  if (!err && (flags & O_EXEC))
    err = treefs_node_access (node, S_IEXEC, auth);
  if (!err && (flags & O_WRITE))
    {
      if (type == S_IFDIR)
	err = EISDIR;
      else if (auth->po->node->fsys->readonly)
	err = EROFS;
      else
	err = treefs_node_access (node, S_IWRITE, auth);
    }

  return err;
}
