/* libdiskfs implementation of fs.defs: file_set_translator
   Copyright (C) 1992,93,94,95,96,99,2001,02,13,14
     Free Software Foundation, Inc.

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
#include <sys/sysmacros.h>
#include <hurd/paths.h>
#include <hurd/fsys.h>

/* Implement file_set_translator as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_set_translator (struct protid *cred,
			      int passive_flags,
			      int active_flags,
			      int killtrans_flags,
			      data_t passive,
			      size_t passivelen,
			      fsys_t active)
{
  struct node *np;
  error_t err;
  mach_port_t control = MACH_PORT_NULL;

  if (!cred)
    return EOPNOTSUPP;

  if (!(passive_flags & FS_TRANS_SET) && !(active_flags & FS_TRANS_SET))
    return 0;

  if ((passive_flags & FS_TRANS_SET) && diskfs_check_readonly ())
    return EROFS;

  if (passivelen && passive[passivelen - 1])
    return EINVAL;

  np = cred->po->np;

  pthread_mutex_lock (&np->lock);

  err = fshelp_isowner (&np->dn_stat, cred->user);
  if (err)
    {
      pthread_mutex_unlock (&np->lock);
      return err;
    }

  if ((active_flags & FS_TRANS_SET)
      && ! (active_flags & FS_TRANS_ORPHAN))
    {
      err = fshelp_fetch_control (&np->transbox, &control);
      if (err)
	{
	  pthread_mutex_unlock (&np->lock);
	  return err;
	}

      if ((control != MACH_PORT_NULL) && ((active_flags & FS_TRANS_EXCL) == 0))
	{
	  pthread_mutex_unlock (&np->lock);
	  err = fsys_goaway (control, killtrans_flags);
	  mach_port_deallocate (mach_task_self (), control);
	  if (err && (err != MIG_SERVER_DIED)
	      && (err != MACH_SEND_INVALID_DEST))
	    return err;
	  err = 0;
	  pthread_mutex_lock (&np->lock);
	}
      else if (control != MACH_PORT_NULL)
	mach_port_deallocate (mach_task_self (), control);
    }

  /* Handle exclusive passive bit *first*.  */
  if ((passive_flags & FS_TRANS_SET)
      && (passive_flags & FS_TRANS_EXCL)
      && (np->dn_stat.st_mode & S_IPTRANS))
    {
      pthread_mutex_unlock (&np->lock);
      return EBUSY;
    }

  if (active_flags & FS_TRANS_SET)
    {
      err = fshelp_set_active (&np->transbox, active,
				 active_flags & FS_TRANS_EXCL);
      if (err)
	{
	  pthread_mutex_unlock (&np->lock);
	  return err;
	}
    }

  /* Set passive translator */
  if (passive_flags & FS_TRANS_SET)
    {
      if (!(passive_flags & FS_TRANS_FORCE))
	{
	  /* Handle the short-circuited translators */
	  mode_t newmode = 0;

	  if (diskfs_shortcut_symlink && !strcmp (passive, _HURD_SYMLINK))
	    newmode = S_IFLNK;
	  else if (diskfs_shortcut_chrdev && !(strcmp (passive, _HURD_CHRDEV)))
	    newmode = S_IFCHR;
	  else if (diskfs_shortcut_blkdev && !strcmp (passive, _HURD_BLKDEV))
	    newmode = S_IFBLK;
	  else if (diskfs_shortcut_fifo && !strcmp (passive, _HURD_FIFO))
	    newmode = S_IFIFO;
	  else if (diskfs_shortcut_ifsock && !strcmp (passive, _HURD_IFSOCK))
	    newmode = S_IFSOCK;

	  if (newmode)
	    {
	      if (S_ISDIR (np->dn_stat.st_mode))
		{
		  /* We can't allow this, because if the mode of the directory
		     changes, the links will be lost.  Perhaps it might be
		     allowed for empty directories, but that's too much of a
		     pain.  */
		  pthread_mutex_unlock (&np->lock);
		  return EISDIR;
		}
	      if (newmode == S_IFBLK || newmode == S_IFCHR)
		{
		  /* Find the device number from the arguments
		     of the translator. */
		  int major, minor;
		  char *arg;

		  arg = passive + strlen (passive) + 1;
		  assert_backtrace (arg <= passive + passivelen);
		  if (arg == passive + passivelen)
		    {
		      pthread_mutex_unlock (&np->lock);
		      return EINVAL;
		    }
		  major = strtol (arg, 0, 0);

		  arg = arg + strlen (arg) + 1;
		  assert_backtrace (arg < passive + passivelen);
		  if (arg == passive + passivelen)
		    {
		      pthread_mutex_unlock (&np->lock);
		      return EINVAL;
		    }
		  minor = strtol (arg, 0, 0);

		  err = diskfs_validate_rdev_change (np,
						       gnu_dev_makedev (major, minor));
		  if (err)
		    {
		      pthread_mutex_unlock (&np->lock);
		      return err;
		    }
		  np->dn_stat.st_rdev = gnu_dev_makedev (major, minor);
		}

	      err = diskfs_truncate (np, 0);
	      if (err)
		{
		  pthread_mutex_unlock (&np->lock);
		  return err;
		}

	      err = diskfs_set_translator (np, NULL, 0, cred);
	      if (err)
		{
		  pthread_mutex_unlock (&np->lock);
		  return err;
		}

	      if (newmode == S_IFLNK)
		{
		  char *arg = passive + strlen (passive) + 1;
		  assert_backtrace (arg <= passive + passivelen);
		  if (arg == passive + passivelen)
		    {
		      pthread_mutex_unlock (&np->lock);
		      return EINVAL;
		    }

		  if (diskfs_create_symlink_hook)
		    err = (*diskfs_create_symlink_hook)(np, arg);
		  if (!diskfs_create_symlink_hook || err == EINVAL)
		    /* Store the argument in the file as the
		       target of the link */
		    err = diskfs_node_rdwr (np, arg, 0, strlen (arg),
					      1, cred, 0);
		  if (err)
		    {
		      pthread_mutex_unlock (&np->lock);
		      return err;
		    }
		}
	      newmode = (np->dn_stat.st_mode & ~S_IFMT) | newmode;
	      err = diskfs_validate_mode_change (np, newmode);
	      if (!err)
		{
		  np->dn_stat.st_mode = newmode;
		  diskfs_node_update (np, diskfs_synchronous);
		}
	      pthread_mutex_unlock (&np->lock);
	      return err;
	    }
	}
      err = diskfs_set_translator (np, passive, passivelen, cred);
    }

  pthread_mutex_unlock (&np->lock);

  if (! err && cred->po->path && active_flags & FS_TRANS_SET)
    err = fshelp_set_active_translator (&cred->pi, cred->po->path, &np->transbox);

  return err;
}
