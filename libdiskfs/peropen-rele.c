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

/* Decrement the reference count on a peropen structure. */
void
diskfs_release_peropen (struct peropen *po)
{
  mutex_lock (&po->np->lock);
  if (--po->refcnt)
    {
      mutex_unlock (&po->np->lock);
      return;
    }
  diskfs_nput (po->np);
  free (po);
}

  
