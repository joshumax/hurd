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

/* Implement io_prenotify as described in <hurd/io.defs>. 

   We set the prenotify size to be the allocated size of the file;
   then users are forced to call this routine before writing past
   that, and we can do allocation (orreturn ENOSPC if necessary. */
error_t
diskfs_S_io_prenotify (struct protid *cred,
		       int start,
		       int end)
{
  struct node *np;
  int err = 0;
  
  if (!cred)
    return EOPNOTSUPP;
  
  np = cred->po->np;

  /* Clamp it down */
  mutex_lock (&np->lock);

  if (!cred->mapped)
    {
      err = EINVAL;
      goto out;
    }

  err = ioserver_verify_user_conch (&np->conch, cred);
  if (err)
    goto out;
  
  ioserver_fetch_shared_data (cred);
  
  if (end < np->allocsize)
    {
      /* The user is either foolin' with us, or has the wrong
	 prenotify size, hence the diagnostic. */
      printf ("io_prenotify: unnecessary call\n");
      spin_lock (&cred->mapped->lock);
      err = ioserver_put_shared_data (cred);
      spin_unlock (&cred->mapped->lock);
      goto out;
    }
  
  err =  file_extend (np, end, cred);
 out:
  mutex_unlock (&np->i_toplock);
  return err;
}
