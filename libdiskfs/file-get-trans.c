/* libdiskfs implementation of fs.defs: file_get_translator
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

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
#include <hurd/paths.h>
#include <string.h>
#include "fs_S.h"

/* Implement file_get_translator as described in <hurd/fs.defs>. */
error_t
diskfs_S_file_get_translator (struct protid *cred,
			      char **trans,
			      u_int *translen)
{
  struct node *np;
  error_t error;
  
  if (!cred)
    return EOPNOTSUPP;
  
  np = cred->po->np;

  mutex_lock (&np->lock);

  /* First look for short-circuited translators. */
  if (S_ISLNK (np->dn_stat.st_mode))
    {
      int len = sizeof _HURD_SYMLINK + np->dn_stat.st_size + 1;
      int amt;
      assert (diskfs_shortcut_symlink);
      if (len  > *translen)
	vm_allocate (mach_task_self (), (vm_address_t *)trans, len, 1);
      bcopy (_HURD_SYMLINK, *trans, sizeof _HURD_SYMLINK);
      error = diskfs_node_rdwr (np, *trans + sizeof _HURD_SYMLINK,
				0, np->dn_stat.st_size, 0, cred, &amt);
      if (!error)
	{
	  assert (amt == np->dn_stat.st_size);
	  (*trans)[sizeof _HURD_SYMLINK + np->dn_stat.st_size] = '\0';
	}
    }
  /* XXX should also do IFCHR, IFBLK, IFIFO, and IFSOCK here. */
  else
    {
      if (!diskfs_node_translated (np))
	error = EINVAL;
      else
	error = diskfs_get_translator (np, trans, translen);
    }
  
  mutex_unlock (&np->lock);

  return error;
}
