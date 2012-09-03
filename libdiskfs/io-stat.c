/*
   Copyright (C) 1994,95,96,97,2001 Free Software Foundation, Inc.

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

#include "priv.h"
#include "io_S.h"
#include <string.h>

/* Implement io_stat as described in <hurd/io.defs>. */
kern_return_t
diskfs_S_io_stat (struct protid *cred,
		  io_statbuf_t *statbuf)
{
  struct node *np;

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;
  pthread_mutex_lock (&np->lock);

  iohelp_get_conch (&np->conch);
  if (diskfs_synchronous)
    diskfs_node_update (np, 1);
  else
    diskfs_set_node_times (np);

  memcpy (statbuf, &np->dn_stat, sizeof (struct stat));
  statbuf->st_mode &= ~(S_IATRANS | S_IROOT);
  if (fshelp_translated (&np->transbox))
    statbuf->st_mode |= S_IATRANS;
  if (cred->po->shadow_root == np || np == diskfs_root_node)
    statbuf->st_mode |= S_IROOT;

  pthread_mutex_unlock (&np->lock);

  return 0;
}
