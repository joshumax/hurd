/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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

#include "priv.h"

/* Callback function needed for calls to fshelp_fetch_root.  See
   <hurd/fshelp.h> for the interface description.  */
static error_t
_diskfs_translator_callback_fn (void *cookie1, void *cookie2,
				mach_port_t *underlying,
				uid_t *uid, gid_t *id, char **argz, 
				int *argz_len, mach_port_t dotdot)
{
  struct node *np = cookie1;
  mach_port_t *dotdot = cookie2;
  error_t err;

  if (!np->istranslated)
    return ENOENT;

  err = diskfs_get_translator (np, argz, (u_int *) argz_len);
  if (err)
    return err;

  *uid = np->dn_stat.st_owner;
  *gid = np->dn_stat.st_group;
  
  *underlying = (ports_get_right
		 (diskfs_make_protid
		  (diskfs_make_peropen (np, 
					(O_READ|O_EXEC|
					 | (diskfs_readonly ? O_WRITE : 0)),
					*dotdot),
		   uid, 1, gid, 1)));
  mach_port_insert_right (mach_task_mself (), *underlying, *underlying,
			  MACH_MSG_TYPE_MAKE_SEND);
  return 0;
}

  
fshelp_callback_t _diskfs_translator_callback = _diskfs_translator_callback_fn;
