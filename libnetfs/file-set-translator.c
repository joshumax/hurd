/*
   Copyright (C) 1996, 1997, 1999, 2001, 2013, 2014
     Free Software Foundation, Inc.
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
#include "fs_S.h"
#include <sys/sysmacros.h>
#include <hurd/paths.h>
#include <hurd/fsys.h>

error_t
netfs_S_file_set_translator (struct protid *user,
			     int passive_flags, int active_flags,
			     int killtrans_flags, data_t passive,
			     mach_msg_type_number_t passivelen,
			     mach_port_t active)
{
  struct node *np;
  error_t err = 0;
  mach_port_t control;

  if (!user)
    return EOPNOTSUPP;

  if (!(passive_flags & FS_TRANS_SET) && !(active_flags & FS_TRANS_SET))
    return 0;

  if (passivelen && passive[passivelen - 1])
    return EINVAL;

  np = user->po->np;
  pthread_mutex_lock (&np->lock);

  if (active_flags & FS_TRANS_SET
      && ! (active_flags & FS_TRANS_ORPHAN))
    {
      /* Validate--user must be owner */
      err = netfs_validate_stat (np, user->user);
      if (err)
	goto out;

      err = fshelp_isowner (&np->nn_stat, user->user);
      if (err)
	goto out;

      err = fshelp_fetch_control (&np->transbox, &control);
      if (err)
	goto out;

      if (control != MACH_PORT_NULL
	  && (active_flags & FS_TRANS_EXCL) == 0)
	{
	  pthread_mutex_unlock (&np->lock);
	  err = fsys_goaway (control, killtrans_flags);
	  if (err && err != MIG_SERVER_DIED && err != MACH_SEND_INVALID_DEST)
	    return err;
	  err = 0;
	  pthread_mutex_lock (&np->lock);
	}
    }

  if ((passive_flags & FS_TRANS_SET)
      && (passive_flags & FS_TRANS_EXCL))
    {
      err = netfs_validate_stat (np, user->user);
      if (!err && (np->nn_stat.st_mode & S_IPTRANS))
	err = EBUSY;
      if (err)
	goto out;
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

	  if (!strcmp (passive, _HURD_SYMLINK))
	    newmode = S_IFLNK;
	  else if (!(strcmp (passive, _HURD_CHRDEV)))
	    newmode = S_IFCHR;
	  else if (!strcmp (passive, _HURD_BLKDEV))
	    newmode = S_IFBLK;
	  else if (!strcmp (passive, _HURD_FIFO))
	    newmode = S_IFIFO;
	  else if (!strcmp (passive, _HURD_IFSOCK))
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

	  err = netfs_attempt_mkdev (user->user, np,
				     newmode, gnu_dev_makedev (major, minor));
	  if (err == EOPNOTSUPP)
	    goto fallback;
	  break;

	case S_IFLNK:
	  arg = passive + strlen (passive) + 1;
	  assert_backtrace (arg <= passive + passivelen);
	  if (arg == passive + passivelen)
	    {
	      pthread_mutex_unlock (&np->lock);
	      return EINVAL;
	    }

	  err = netfs_attempt_mksymlink (user->user, np, arg);
	  if (err == EOPNOTSUPP)
	    goto fallback;
	  break;

	default:
	  err = netfs_validate_stat (np, user->user);
	  if (!err)
	    err = netfs_attempt_chmod (user->user, np,
				       ((np->nn_stat.st_mode & ~S_IFMT)
					| newmode));
	  if (err == EOPNOTSUPP)
	    goto fallback;
	  break;

	case 0:
	fallback:
	  err = netfs_set_translator (user->user, np,
				      passive, passivelen);
	  break;
	}
    }

  if (! err && user->po->path && active_flags & FS_TRANS_SET)
    err = fshelp_set_active_translator (&user->pi, user->po->path, &np->transbox);

 out:
  pthread_mutex_unlock (&np->lock);
  return err;
}
