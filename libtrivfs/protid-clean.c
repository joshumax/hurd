/* 
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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
  
  if (trivfs_protid_destroy_hook && cred->realnode != MACH_PORT_NULL)
    /* Allow the user to clean up; If the realnode field is null, then CRED
       wasn't initialized to the point of needing user cleanup.  */
    (*trivfs_protid_destroy_hook) (cred);

  /* If we hold the only reference to the peropen, try to get rid of it. */
  pthread_mutex_lock (&cred->po->cntl->lock);
  if (cred->po->refcnt == 1 && trivfs_peropen_destroy_hook)
    {
      pthread_mutex_unlock (&cred->po->cntl->lock);
      (*trivfs_peropen_destroy_hook) (cred->po);
      pthread_mutex_lock (&cred->po->cntl->lock);
    }
  if (--cred->po->refcnt == 0)
    {
      ports_port_deref (cred->po->cntl);
      free (cred->po);
    }
  pthread_mutex_unlock (&cred->po->cntl->lock);

  iohelp_free_iouser (cred->user);

  if (cred->realnode != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), cred->realnode);
}

  

