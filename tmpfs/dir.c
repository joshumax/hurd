/* Directories for tmpfs.
   Copyright (C) 2000,01,02 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stddef.h>
#include <unistd.h>
#include <sys/mman.h>

#include "tmpfs.h"
#include <stdlib.h>

error_t
diskfs_init_dir (struct node *dp, struct node *pdp, struct protid *cred)
{
  dp->dn->u.dir.dotdot = pdp->dn;
  dp->dn->u.dir.entries = 0;

  /* Increase hardlink count for parent directory */
  pdp->dn_stat.st_nlink++;
  /* Take '.' directory into account */
  dp->dn_stat.st_nlink++;

  return 0;
}

error_t
diskfs_clear_directory (struct node *dp, struct node *pdp,
			struct protid *cred)
{
  if (dp->dn->u.dir.entries != 0)
    return ENOTEMPTY;
  assert_backtrace (dp->dn_stat.st_size == 0);
  assert_backtrace (dp->dn->u.dir.dotdot == pdp->dn);

  /* Decrease hardlink count for parent directory */
  pdp->dn_stat.st_nlink--;
  /* Take '.' directory into account */
  dp->dn_stat.st_nlink--;

  return 0;
}

int
diskfs_dirempty (struct node *dp, struct protid *cred)
{
  return dp->dn->u.dir.entries == 0;
}

error_t
diskfs_get_directs (struct node *dp, int entry, int n,
		    char **data, size_t *datacnt,
		    vm_size_t bufsiz, int *amt)
{
  struct tmpfs_dirent *d;
  struct dirent *entp;
  int i;

  if (bufsiz == 0)
    bufsiz = dp->dn_stat.st_size
	     + 2 * ((offsetof (struct dirent, d_name[3]) + 7) & ~7);
  if (bufsiz > *datacnt)
    {
      *data = mmap (0, bufsiz, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (*data == MAP_FAILED)
	return ENOMEM;
    }

  /* We always synthesize the first two entries (. and ..) on the fly.  */
  entp = (struct dirent *) *data;
  i = 0;
  if (i++ >= entry)
    {
      entp->d_fileno = dp->dn_stat.st_ino;
      entp->d_type = DT_DIR;
      entp->d_namlen = 1;
      entp->d_name[0] = '.';
      entp->d_name[1] = '\0';
      entp->d_reclen = (&entp->d_name[2] - (char *) entp + 7) & ~7;
      entp = (void *) entp + entp->d_reclen;
    }
  if (i++ >= entry)
    {
      if (dp->dn->u.dir.dotdot == 0)
	{
	  assert_backtrace (dp == diskfs_root_node);
	  /* Use something not zero and not an st_ino value for any node in
	     this filesystem.  Since we use pointer values, 2 will never
	     be a valid number.  */
	  entp->d_fileno = 2;
	}
      else
	entp->d_fileno = (ino_t) (uintptr_t) dp->dn->u.dir.dotdot;
      entp->d_type = DT_DIR;
      entp->d_namlen = 2;
      entp->d_name[0] = '.';
      entp->d_name[1] = '.';
      entp->d_name[2] = '\0';
      entp->d_reclen = (&entp->d_name[3] - (char *) entp + 7) & ~7;
      entp = (void *) entp + entp->d_reclen;
    }

  /* Skip ahead to the desired entry.  */
  for (d = dp->dn->u.dir.entries; i < entry && d != 0; d = d->next)
    ++i;

  if (i < entry)
    {
      assert_backtrace (d == 0);
      *datacnt = 0;
      *amt = 0;
      return 0;
    }

  /* Now fill in the buffer with real entries.  */
  for (; d != 0; d = d->next, i++)
    {
      size_t rlen = (offsetof (struct dirent, d_name[1]) + d->namelen + 7) & ~7;
      if (rlen + (char *) entp - *data > bufsiz || (n >= 0 && i > n))
	break;
      entp->d_fileno = (ino_t) (uintptr_t) d->dn;
      entp->d_type = DT_UNKNOWN;
      entp->d_namlen = d->namelen;
      memcpy (entp->d_name, d->name, d->namelen + 1);
      entp->d_reclen = rlen;
      entp = (void *) entp + rlen;
    }

  *datacnt = (char *) entp - *data;
  *amt = i - entry;

  return 0;
}


struct dirstat
{
  struct tmpfs_dirent **prevp;
  int dotdot;
};
const size_t diskfs_dirstat_size = sizeof (struct dirstat);

void
diskfs_null_dirstat (struct dirstat *ds)
{
  ds->prevp = 0;
}

error_t
diskfs_drop_dirstat (struct node *dp, struct dirstat *ds)
{
  /* No need to clear the pointers.  */
  return 0;
}

error_t
diskfs_lookup_hard (struct node *dp,
		    const char *name, enum lookup_type type,
		    struct node **np, struct dirstat *ds,
		    struct protid *cred)
{
  const size_t namelen = strlen (name);
  struct tmpfs_dirent *d, **prevp;

  if (type == REMOVE || type == RENAME)
    assert_backtrace (np);

  if (ds)
    ds->dotdot = type & SPEC_DOTDOT;

  if (namelen == 1 && name[0] == '.')
    {
      if (np != 0)
	{
	  *np = dp;
	  diskfs_nref (dp);
	}
      return 0;
    }
  if (namelen == 2 && name[0] == '.' && name[1] == '.')
    {
      struct disknode *dddn = dp->dn->u.dir.dotdot;
      error_t err;

      assert_backtrace (np != 0);
      if (dddn == 0)		/* root directory */
	return EAGAIN;

      if (type == (REMOVE|SPEC_DOTDOT) || type == (RENAME|SPEC_DOTDOT))
        {
	  *np = *dddn->hprevp;
	  assert_backtrace (*np);
	  assert_backtrace ((*np)->dn == dddn);
	  assert_backtrace (*dddn->hprevp == *np);
	  return 0;
	}
      else
        {
	  pthread_mutex_unlock (&dp->lock);
          err = diskfs_cached_lookup ((ino_t) (intptr_t) dddn, np);

	  if (type == (LOOKUP|SPEC_DOTDOT))
	    diskfs_nrele (dp);
	  else
	    pthread_mutex_lock (&dp->lock);

	  if (err)
	    *np = 0;

          return err;
	}
    }

  for (d = *(prevp = &dp->dn->u.dir.entries); d != 0;
       d = *(prevp = &d->next))
    if (d->namelen == namelen && !memcmp (d->name, name, namelen))
      {
	if (ds)
	  ds->prevp = prevp;

	if (np)
	  return diskfs_cached_lookup ((ino_t) (uintptr_t) d->dn, np);
	else
	  return 0;
      }

  if (ds)
    ds->prevp = prevp;
  if (np)
    *np = 0;
  return ENOENT;
}


error_t
diskfs_direnter_hard (struct node *dp, const char *name,
		      struct node *np, struct dirstat *ds,
		      struct protid *cred)
{
  const size_t namelen = strlen (name);
  const size_t entsize
	  = (offsetof (struct dirent, d_name[1]) + namelen + 7) & ~7;
  struct tmpfs_dirent *new;

  if (round_page (tmpfs_space_used + entsize) / vm_page_size
      > tmpfs_page_limit)
    return ENOSPC;

  new = malloc (offsetof (struct tmpfs_dirent, name) + namelen + 1);
  if (new == 0)
    return ENOSPC;

  new->next = 0;
  new->dn = np->dn;
  new->namelen = namelen;
  memcpy (new->name, name, namelen + 1);
  *ds->prevp = new;

  dp->dn_stat.st_size += entsize;
  adjust_used (entsize);

  dp->dn_stat.st_blocks = ((sizeof *dp->dn + dp->dn->translen
			    + dp->dn_stat.st_size + 511)
			   / 512);
  return 0;
}

error_t
diskfs_dirrewrite_hard (struct node *dp, struct node *np,
			struct dirstat *ds)
{
  if (ds->dotdot)
    dp->dn->u.dir.dotdot = np->dn;
  else
    (*ds->prevp)->dn = np->dn;

  return 0;
}

error_t
diskfs_dirremove_hard (struct node *dp, struct dirstat *ds)
{
  struct tmpfs_dirent *d = *ds->prevp;
  const size_t entsize
	  = (offsetof (struct dirent, d_name[1]) + d->namelen + 7) & ~7;

  *ds->prevp = d->next;

  if (dp->dirmod_reqs != 0)
    diskfs_notice_dirchange (dp, DIR_CHANGED_UNLINK, d->name);

  free (d);

  adjust_used (-entsize);
  dp->dn_stat.st_size -= entsize;
  dp->dn_stat.st_blocks = ((sizeof *dp->dn + dp->dn->translen
			    + dp->dn_stat.st_size + 511)
			   / 512);

  return 0;
}
