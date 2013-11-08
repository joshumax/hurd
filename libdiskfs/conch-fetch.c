/* 
   Copyright (C) 1994, 1995, 1996, 1999 Free Software Foundation

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
#include <hurd/iohelp.h>

/* Update our copy of the relevant fields from a shared page.  Callers
   must have the share lock on the shared page as well as the inode
   toplock.  This is called by the conch management facilities of 
   libioserver as well as by us.  */
void
iohelp_fetch_shared_data (void *arg)
{
  struct protid *cred = arg;
  int mod = 0;
  
  /* Don't allow the user to grow the file past the alloc size. */
  if (cred->mapped->file_size > cred->po->np->allocsize)
    cred->mapped->file_size = cred->po->np->allocsize;

  /* Don't allow the user to truncate the file this way. */
  if (cred->mapped->file_size < cred->po->np->dn_stat.st_size)
    cred->mapped->file_size = cred->po->np->dn_stat.st_size;
  else if (cred->po->np->dn_stat.st_size != cred->mapped->file_size)
    {
      /* The user can validly set the size, but block the attempt
	 if we are readonly. */
      if (diskfs_check_readonly ())
	cred->mapped->file_size = cred->po->np->dn_stat.st_size;
      else
	{
	  cred->po->np->dn_stat.st_size = cred->mapped->file_size;
	  cred->po->np->dn_set_ctime = 1;
	  mod = 1;
	}
    }
  
  cred->po->filepointer = cred->mapped->xx_file_pointer;
      
  if (!diskfs_check_readonly ())
    {
      if (cred->mapped->written)
	{
	  cred->po->np->dn_set_mtime = 1;
	  mod = 1;
	}
      if (cred->mapped->accessed && ! _diskfs_noatime)
	{
	  cred->po->np->dn_set_atime = 1;
	  mod = 1;
	}
    }
  cred->mapped->written = 0;
  cred->mapped->accessed = 0;
  if (diskfs_synchronous && mod)
    diskfs_node_update (cred->po->np, 1);
}
