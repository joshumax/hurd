/* libdiskfs implementation of fs.defs: dir_unlink
   Copyright (C) 1992, 1993, 1994, 1995 Free Software Foundation

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

/* Implement dir_unlink as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_dir_unlink (struct protid *dircred,
		     char *name)
{
  struct node *dnp;
  struct node *np;
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  error_t error;

  if (!dircred)
    return EOPNOTSUPP;
  
  dnp = dircred->po->np;
  if (diskfs_readonly)
    return EROFS;

 retry:
  
  mutex_lock (&dnp->lock);

  error = diskfs_lookup (dnp, name, REMOVE, &np, ds, dircred);
  if (error == EAGAIN)
    error = EISDIR;
  if (error)
    {
      diskfs_drop_dirstat (dnp, ds);
      mutex_unlock (&dnp->lock);
      return error;
    }
  
  /* This isn't the BSD behavior, but it is Posix compliant and saves
     us on several race conditions.*/
  if (S_ISDIR(np->dn_stat.st_mode))
    {
      if (np == dnp)		/* gotta catch '.' */
	diskfs_nrele (np);
      else
	diskfs_nput (np);
      diskfs_drop_dirstat (dnp, ds);
      mutex_unlock (&dnp->lock);
      return EISDIR;
    }

  mutex_lock (&np->translator.lock);
  if (np->translator.control != MACH_PORT_NULL)
    {
      /* There is a running active translator here.  Give it a push.
	 If it squeaks, then return an error.  If it consents, then
	 clear the active translator spec (unless it's been changed
	 in the interim) and repeat the lookup above.  */

      control = np->translator.control;
      mach_port_mod_refs (mach_task_self (), control, MACH_PORT_RIGHT_SEND, 1);

      mutex_unlock (&np->translator.lock);
      diskfs_drop_dirstat (dnp, ds);
      mutex_unlock (&dnp->lock);
      mutex_unlock (&np->lock);
      
      error = fsys_goaway (control, FSYS_GOAWAY_UNLINK);
      if (error)
	return error;
      
      mutex_lock (&np->lock);
      mutex_lock (&np->translator.lock);
      if (np->translator.control == control)
	fshelp_translator_drop (&np->translator);
      mutex_unlock (&np->translator.lock);
      diskfs_nput (np);
      
      mach_port_deallocate (mach_task_self (), control);

      goto retry;
    }
  mutex_unlock (&np->translator.lock);

  error = diskfs_dirremove (dnp, ds);
  if (diskfs_synchronous)
    diskfs_node_update (dnp, 1);
  if (error)
    {
      diskfs_nput (np);
      mutex_unlock (&dnp->lock);
      return error;
    }
      
  np->dn_stat.st_nlink--;
  np->dn_set_ctime = 1;
  if (diskfs_synchronous)
    diskfs_node_update (np, 1);

  /* This check is necessary because we might get here on an error while 
     checking the mode on something which happens to be `.'. */
  if (np == dnp)
    diskfs_nrele (np);	
  else
    diskfs_nput (np);
  mutex_unlock (&dnp->lock);
  return error;
}
