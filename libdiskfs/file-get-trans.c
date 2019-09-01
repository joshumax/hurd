/* libdiskfs implementation of fs.defs: file_get_translator
   Copyright (C) 1992,93,94,95,96,98,99,2002 Free Software Foundation, Inc.

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
#include <stdio.h>
#include "fs_S.h"

/* Implement file_get_translator as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_get_translator (struct protid *cred,
			      data_t *trans,
			      size_t *translen)
{
  struct node *np;
  error_t err = 0;

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;

  pthread_mutex_lock (&np->lock);

  /* First look for short-circuited translators. */
  if (S_ISLNK (np->dn_stat.st_mode))
    {
      unsigned int len = sizeof _HURD_SYMLINK + np->dn_stat.st_size + 1;
      size_t amt;
      assert_backtrace (diskfs_shortcut_symlink);
      if (len > *translen)
	*trans = mmap (0, len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      memcpy (*trans, _HURD_SYMLINK, sizeof _HURD_SYMLINK);

      if (diskfs_read_symlink_hook)
	err = (*diskfs_read_symlink_hook) (np,
					     *trans + sizeof _HURD_SYMLINK);
      if (!diskfs_read_symlink_hook || err == EINVAL)
	{
	  err = diskfs_node_rdwr (np, *trans + sizeof _HURD_SYMLINK,
				    0, np->dn_stat.st_size, 0, cred, &amt);
	  if (!err)
	    assert_backtrace (amt == np->dn_stat.st_size);
	}
      if (!err)
	{
	  (*trans)[sizeof _HURD_SYMLINK + np->dn_stat.st_size] = '\0';
	  *translen = len;
	}
      else if (len > *translen)
	munmap (trans, len);
    }
  else if (S_ISCHR (np->dn_stat.st_mode) || S_ISBLK (np->dn_stat.st_mode))
    {
      char *buf;
      unsigned int buflen;

      if (S_ISCHR (np->dn_stat.st_mode))
	assert_backtrace (diskfs_shortcut_chrdev);
      else
	assert_backtrace (diskfs_shortcut_blkdev);

      buflen = asprintf (&buf, "%s%c%d%c%d",
			 (S_ISCHR (np->dn_stat.st_mode)
			  ? _HURD_CHRDEV
			  : _HURD_BLKDEV),
			 '\0', (np->dn_stat.st_rdev >> 8) & 0377,
			 '\0', (np->dn_stat.st_rdev) & 0377);
      buflen++;			/* terminating nul */

      if (buflen > *translen)
	*trans = mmap (0, buflen, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      memcpy (*trans, buf, buflen);
      free (buf);
      *translen = buflen;
      err = 0;
    }
  else if (S_ISFIFO (np->dn_stat.st_mode))
    {
      unsigned int len;

      len = sizeof _HURD_FIFO;
      if (len > *translen)
	*trans = mmap (0, len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      memcpy (*trans, _HURD_FIFO, sizeof _HURD_FIFO);
      *translen = len;
      err = 0;
    }
  else if (S_ISSOCK (np->dn_stat.st_mode))
    {
      unsigned int len;

      len = sizeof _HURD_IFSOCK;
      if (len > *translen)
	*trans = mmap (0, len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      memcpy (*trans, _HURD_IFSOCK, sizeof _HURD_IFSOCK);
      *translen = len;
      err = 0;
    }
  else
    {
      if (! (np->dn_stat.st_mode & S_IPTRANS))
	err = EINVAL;
      else
	{
	  char *string;
	  u_int len;
	  err = diskfs_get_translator (np, &string, &len);
	  if (!err)
	    {
	      if (len > *translen)
		*trans = mmap (0, len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	      memcpy (*trans, string, len);
	      *translen = len;
	      free (string);
	    }
	}
    }

  pthread_mutex_unlock (&np->lock);

  return err;
}
