/* 
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

#include "priv.h"

/* Clean pointers in a struct trivfs_protid when its last reference vanishes
   before it's freed.  */
void
trivfs_clean_protid (void *arg)
{
  struct trivfs_protid *cred = arg;
  
  if (trivfs_protid_destroy_hook)
    (*trivfs_protid_destroy_hook) (cred);
  if (!cred->po->refcnt--)
    {
      if (trivfs_peropen_destroy_hook)
	(*trivfs_peropen_destroy_hook) (cred->po);
      ports_done_with_port (cred->po->cntl);
      free (cred->po);
    }
  free (cred->uids);
  free (cred->gids);
  mach_port_deallocate (mach_task_self (), cred->realnode);
}

  

