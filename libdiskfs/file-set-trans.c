/* libdiskfs implementation of fs.defs: file_set_translator
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
#include "fs_S.h"
#include <hurd/paths.h>

/* Implement file_set_translator as described in <hurd/fs.defs>. */
error_t
diskfs_S_file_set_translator (struct protid *cred,
			      int flags,
			      int killtrans_flags,
			      char *transname,
			      u_int transnamelen,
			      fsys_t existing)
{
  struct node *np;
  error_t error;

  if (!cred)
    return EOPNOTSUPP;
  
  if (!transnamelen && existing == MACH_PORT_NULL)
    return 0;

  np = cred->po->np;

  if (transnamelen && !transname[transnamelen - 1])
    return EINVAL;

  mutex_lock (&np->lock);

  if (error = diskfs_isowner (np, cred))
    {
      mutex_unlock (&np->lock);
      return error;
    }

  if (np->translator.control != MACH_PORT_NULL)
    {
      if (flags & FS_TRANS_EXCL)
	{
	  mutex_unlock (&np->lock);
	  return EBUSY;
	}
      diskfs_destroy_translator (np, killtrans_flags);
    }

  if ((flags & FS_TRANS_EXCL) && transname && diskfs_node_translated (np))
    {
      mutex_unlock (&np->lock);
      return EBUSY;
    }
	  
  if (transnamelen)
    {
      if (!(flags & FS_TRANS_FORCE))
	{
	  /* Handle the short-circuited translators */
	  mode_t newmode = 0;
	
	  if (diskfs_shortcut_symlink && !strcmp (transname, _HURD_SYMLINK))
	    newmode = S_IFLNK;
	  if (diskfs_shortcut_chrdev && !(strcmp (transname, _HURD_CHRDEV)))
	    newmode = S_IFCHR;
	  else if (diskfs_shortcut_blkdev && !strcmp (transname, _HURD_BLKDEV))
	    newmode = S_IFBLK;
	  else if (diskfs_shortcut_fifo && !strcmp (transname, _HURD_FIFO))
	    newmode = S_IFIFO;
	  else if (diskfs_shortcut_ifsock && !strcmp (transname, _HURD_IFSOCK))
	    newmode = S_IFSOCK;
	
	  if (newmode)
	    {
	      if (S_ISDIR (np->dn_stat.st_mode))
		{
		  /* We can't allow this, because if the mode of the directory
		     changes, the links will be lost.  Perhaps it might be 
		     allowed for empty directories, but that's too much of a
		     pain.  */
		  mutex_unlock (&np->lock);
		  return EISDIR;
		}
	      if (newmode == S_IFBLK || newmode == S_IFCHR)
		{
		  /* Find the device number from the arguments
		     of the translator. */
		  int major, minor;
		  char *arg;
	      
		  arg = transname + strlen (transname) + 1;
		  assert (arg <= transname + transnamelen);
		  if (arg == transname + transnamelen)
		    {
		      mutex_unlock (&np->lock);
		      return EINVAL;
		    }
		  major = strtol (arg, 0, 0);

		  arg = arg + strlen (arg) + 1;
		  assert (arg < transname + transnamelen);
		  if (arg == transname + transnamelen)
		    {
		      mutex_unlock (&np->lock);
		      return EINVAL;
		    }
		  minor = strtol (arg, 0, 0);
	      
		  np->dn_stat.st_rdev = (((major & 0x377) << 8)
					 | (minor & 0x377));
		}

	      diskfs_truncate (np, 0);
	      if (newmode == S_IFLNK)
		{
		  char *arg = transname + strlen (transname) + 1;
		  assert (arg <= transname + transnamelen);
		  if (arg == transname + transnamelen)
		    {
		      mutex_unlock (&np->lock);
		      return EINVAL;
		    }
		  /* Store the argument in the file as the
		     target of the link */
		  diskfs_node_rdwr (np, arg, 0, strlen (arg), 1, cred, 0);
		}
	      np->dn_stat.st_mode = (np->dn_stat.st_mode & ~S_IFMT) | newmode;
	      diskfs_node_update (np, 1);
	      mutex_unlock (&np->lock);
	      return 0;
	    }
	}
      error = diskfs_set_translator (np, transname, transnamelen, cred);
    }

  if (existing != MACH_PORT_NULL)
    {    
      np->translator.control = existing;
      np->translator.starting = 0;
    }
  mutex_unlock (&np->lock);
  return error;
}
