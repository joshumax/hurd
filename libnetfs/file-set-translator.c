/* 
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "netfs.h"

error_t 
netfs_S_file_set_translator (struct protid *user,
			     int pflags, int aflags,
			     int gflags, char *passive,
			     mach_msg_type_number_t passivelen,
			     mach_port_t active)
{
  struct node *np;
  error_t err;
  uid_t *uids, *gids;
  int nuids, ngids;
  int i;
  mach_port_t control;

  if (!user)
    return EOPNOTSUPP;
  
  if (!(passive_flags & FS_TRANS_SET) && !(active_flags & FS_TRANS_SET))
    return 0;
  
  if (passive && passive[passivelen - 1])
    return EINVAL;
  
  np = user->po->np;
  mutex_lock (&np->lock);
  
  if (active_flags & FS_TRANS_SET)
    {
      /* Validate--user must be owner */
      err = netfs_interpret_credential (user->credential, &uids, &nuids,
					&gids, &ngids);
      if (err)
	goto out;
      err = netfs_validate_stat (np, user->credential);
      if (err)
	goto out;

      for (i = 0; i < nuids; i++)
	if (uids[i] == 0 || uids[i] == np->nn_stat.st_uid)
	  break;
      if (i == uids)
	{
	  mutex_unlock (&np->lock);
	  return EBUSY;
	}

      err = fshelp_fetch_control (&np->transbox, &control);
      if (err)
	goto out;
      
      if (control != MACH_PORT_NULL 
	  && (active_flags & FS_TRANS_EXCL) == 0)
	{
	  mutex_unlock (&np->lock);
	  err = fsys_goaway (control, killtrans_flags);
	  if (err && err != MIG_SERVER_DIED && err != MACH_SEND_INVALID_DEST)
	    return err;
	  err = 0;
	  mutex_lock (&np->lock);
	}
    }
  
  if ((passive_flags & FS_TRANS_SET)
      && (passive_flags & FS_TRANS_EXCL)
      && np->istranslated)
    {
      mutex_unlock (&np->lock);
      return EBUSY;
    }
  
  if (active_flags & FS_TRANS_SET)
    {
      err = fshelp_set_active (&np->transbox, active,
			       active_flags & FS_TRANS_EXCL);
      if (err)
	goto out;
    }
  
  if (passive_flags & FS_TRANS_SET)
    {
      mode_t newmode = 0;
      if (!(passive_flags & FS_TRANS_FORCE))
	{
	  /* Short circuited translators */

	  if (netfs_shortcut_symlink && !strcmp (passive, _HURD_SYMLINK))
	    newmode = S_IFLNK;
	  else if (netfs_shortcut_chrdev && !(strcmp (passive, _HURD_CHRDEV)))
	    newmode = S_IFCHR;
	  else if (netfs_shortcut_blkdev && !strcmp (passive, _HURD_BLKDEV))
	    newmode = S_IFBLK;
	  else if (netfs_shortcut_fifo && !strcmp (passive, _HURD_FIFO))
	    newmode = S_IFIFO;
	  else if (netfs_shortcut_ifsock && !strcmp (passive, _HURD_IFSOCK))
	    newmode = S_IFSOCK;
	}
      
      switch (newmode)
	{
	  int major, minor;
	  char *arg;
	  
	case S_IFBLK:
	case S_IFCHR:
	  /* Find the device number from the arguments
	     of the translator. */
	  arg = passive + strlen (passive) + 1;
	  assert (arg <= passive + passivelen);
	  if (arg == passive + passivelen)
	    {
	      mutex_unlock (&np->lock);
	      return EINVAL;
	    }
	  major = strtol (arg, 0, 0);
	  
	  arg = arg + strlen (arg) + 1;
	  assert (arg < passive + passivelen);
	  if (arg == passive + passivelen)
	    {
	      mutex_unlock (&np->lock);
	      return EINVAL;
	    }
	  minor = strtol (arg, 0, 0);
	  
	  err = netfs_attempt_mkdev (user->credential, np,
				     newmode, major, minor);
	  if (err == EOPNOTSUPP)
	    goto fallback;
	  break;
	  
	case S_IFLNK:
	  arg = passive + strlen (passive) + 1;
	  assert (arg <= passive + passivelen);
	  if (arg == passive + passivelen)
	    {
	      mutex_unlock (&np->lock);
	      return EINVAL;
	    }
	  
	  err = netfs_attempt_mksymlink (user->credential, np, arg);
	  if (err == EOPNOTSUPP)
	    goto fallback;
	  break;

	default:
	  err = netfs_validate_stat (np, user->credential);
	  if (!err)
	    err = netfs_attempt_chmod (user->credential, np,
				       ((np->nn_stat.st_mode & ~S_IFMT)
					| newmode));
	  if (err == EOPNOTSUPP)
	    goto fallback;
	  break;
	  
	case 0:
	fallback:
	  error = netfs_set_translator (np, passive, passivelen, cred);
	  break;
	}
    }
  
 out:
  mutex_unlock (&np->lock);
  return err;
}
