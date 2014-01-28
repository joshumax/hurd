/* libkdiskfs implementation of fs.defs: file_get_translator_cntl
   Copyright (C) 1992, 1993, 1994, 1995, 1996 Free Software Foundation

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
#include "fs_S.h"

/* Implement file_get_translator_cntl as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_get_translator_cntl (struct protid *cred,
				   mach_port_t *ctl,
				   mach_msg_type_name_t *ctltype)
{
  struct node *np;
  error_t err;
  
  if (!cred)
    return EOPNOTSUPP;
  
  np = cred->po->np;

  pthread_mutex_lock (&np->lock);

  err = fshelp_isowner (&np->dn_stat, cred->user);
  if (!err)
    err = fshelp_fetch_control (&np->transbox, ctl);
  if (!err && *ctl == MACH_PORT_NULL)
    err = ENXIO;
  if (!err)
    *ctltype = MACH_MSG_TYPE_MOVE_SEND;

  pthread_mutex_unlock (&np->lock);

  return err;
}
