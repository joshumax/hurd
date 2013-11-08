/* 
   Copyright (C) 1995, 1996, 1997, 2000 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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

#include "netfs.h"
#include "io_S.h"
#include <string.h>

error_t
netfs_S_io_stat (struct protid *user, io_statbuf_t *statbuf)
{
  error_t err;
  struct node *node;
  
  if (! user)
    return EOPNOTSUPP;

  node = user->po->np;
  pthread_mutex_lock (&node->lock);

  err = netfs_validate_stat (node, user->user);
  if (! err)
    {
      memcpy (statbuf, &node->nn_stat, sizeof (struct stat));

      /* Set S_IATRANS and S_IROOT bits as appropriate.  */
      statbuf->st_mode &= ~(S_IATRANS | S_IROOT);
      if (fshelp_translated (&node->transbox))
	statbuf->st_mode |= S_IATRANS; /* Has an active translator.  */
      if (user->po->shadow_root == node || node == netfs_root_node)
	statbuf->st_mode |= S_IROOT; /* Is a root node.  */
    }

  pthread_mutex_unlock (&node->lock);

  return err;
}
