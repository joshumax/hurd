/* Private declarations for fileserver library
   Copyright (C) 1994 Free Software Foundation

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

extern mach_port_t fs_control_port;	/* receive right */

/* Called by MiG to translate ports into struct protid *.  
   fsmutations.h arranges for this to happen for the io and
   fs interfaces. */
extern inline struct protid *
begin_using_protid_port (file_t port)
{
  return ports_check_port_type (port, PT_PROTID);
}

/* Called by MiG after server routines have been run; this
   balances begin_using_protid_port, and is arranged for the io
   and fs interfaces by fsmutations.h. */
extern inline void
diskfs_end_using_protid_port (struct protid *cred)
{
  ports_done_with_port (cred);
}

/* This macro locks the node associated with PROTID, and then
   evaluates the expression OPERATION; then it syncs the inode
   (without waiting) and unlocks everything, and then returns
   the value `err' (which can be set by OPERATION if desired). */
#define CHANGE_NODE_FIELD(PROTID, OPERATION)				    \
({									    \
  error_t err = 0;							    \
  struct node *np;							    \
  									    \
  if (!(PROTID))							    \
    return EOPNOTSUPP;							    \
  									    \
  if (readonly)								    \
    return EROFS;							    \
  									    \
  np = (PROTID)->po->np;						    \
  									    \
  mutex_lock (&np->lock);						    \
  (OPERATION);								    \
  diskfs_node_update (np, 0);						    \
  mutex_unlock (&np->lock);						    \
  return err;								    \
})
