/* Starting a passive translator

   Copyright (C) 1994, 1995, 1999 Free Software Foundation, Inc.

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

#include "treefs.h"

#include <fcntl.h>

int fshelp_transboot_port_type = PT_TRANSBOOT;

/* Start the translator TRANS (of length TRANS_LEN) on NODE, which should be
   locked, and will be unlocked when this function returns.  PARENT_PORT is
   a send right to use as the parent port passed to the translator.  */
error_t
_treefs_node_start_translator (struct treefs_node *node,
			       char *trans, unsigned trans_len,
			       file_t parent_port)
{
  error_t err;
  int mode = O_READ | O_EXEC;
  struct treefs_auth *auth;
  file_t node_port;
  uid_t uid, gid;

  err = treefs_node_get_trans_auth (node, &auth);
  if (err)
    return err;

  if (!node->fsys->readonly && treefs_node_type (node) == S_IFREG)
    mode |= O_WRITE;

  /* Create the REALNODE port for the new filesystem. */
  node_port = treefs_node_make_right (node, mode, parent_port, auth);
  mach_port_insert_right (mach_task_self (), node_port, node_port,
			  MACH_MSG_TYPE_MAKE_SEND);


  pthread_mutex_unlock (&node->lock);
  
  /* XXXX Change libfshelp so that it take more than 1 uid/gid? */
  uid = auth->nuids > 0 ? auth->uids[0] : -1;
  gid = auth->ngids > 0 ? auth->gids[0] : -1;

  /* XXX this should use fshelp_start_translator_long. */
  err =
    fshelp_start_translator (&node->active_trans, NULL, trans, trans_len,
			     parent_port, node_port, uid, gid);

  treefs_node_auth_unref (node, auth);

  return err;
}
