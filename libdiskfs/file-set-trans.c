/* libdiskfs implementation of fs.defs: file_set_translator
   Copyright (C) 1993, 1994 Free Software Foundation

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

/* XXX need to reimplement transnamelen parameter, and implement Roland's
   suggestion:
   For S_IFMT shorcuts, don't read the device number out of the file!
   TRANSNAME should be "ifmt\0minor\0major\0", where IFMT is
   _HURD_{CHR,BLK}DEV and MAJOR, MINOR can be parsed with strtol (,,0)
   (allowing octal or hex with 0 or 0x). */

/* Implement file_set_translator as described in <hurd/fs.defs>. */
error_t
diskfs_S_file_set_translator (struct protid *cred,
			      int flags,
			      int killtrans_flags,
			      char *transname,
			      fsys_t existing)
{
  struct node *np;
  error_t error;
  daddr_t blkno;
  char blkbuf[sblock->fs_fsize];

  if (!cred)
    return EOPNOTSUPP;
  
  np = cred->po->np;

  mutex_lock (&np->lock);

  if (error = isowner (np, cred))
    {
      mutex_unlock (&np->lock);
      return error;
    }

  if (np->translator)
    {
      if (flags & FS_TRANS_EXCL)
	{
	  mutex_unlock (&np->lock);
	  return EBUSY;
	}
      diskfs_destroy_translator (np, killtrans_flags);
    }

  if ((flags & FS_TRANS_EXCL) && diskfs_node_translated (np))
    {
      mutex_unlock (&np->lock);
      return EBUSY;
    }
	  
  /* Handle the short-circuited translators */
  if (!(flags & FS_TRANS_FORCE))
    {
      mode_t newmode = 0;
      
      if (diskfs_shortcut_symlink && !strcmp (transname, _HURD_SYMLINK))
	newmode = S_IFLNK;

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
	      /* Here we need to read the device number from the
		 contents of the file (which must be S_IFREG).  */
	      char buf[20];
	      
	      if (S_ISREG (np->dn_stat.st_mode))
		{
		  mutex_unlock (&np->lock);
		  return EINVAL;
		}
	      error = diskfs_fs_rdwr (np, buf, 0, 20, 0, 0);
	      if (error)
		{
		  mutex_unlock (&np->lock);
		  return error;
		}
	      np->dn_stat.st_rdev = atoi (buf);
	    }
	  if (newmode != IFLNK)
	    node_truncate (np, 0);
	  np->dn_stat.st_mode = (np->dn_stat.st_mode & ~S_IFMT) | newmode;
	  diskfs_node_update (np, 1);
	  mutex_unlock (&np->lock);
	  return 0;
	}
    }

  error = diskfs_set_translator (np, transname, cred);
  mutex_unlock (&np->lock);
  return error;
}
