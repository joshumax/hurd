/* Directories for tmpfs.
   Copyright (C) 2000, 2001 Free Software Foundation, Inc.

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
  return 0;
}

error_t
diskfs_clear_directory (struct node *dp, struct node *pdp,
			struct protid *cred)
{
  if (dp->dn->u.dir.entries != 0)
    return ENOTEMPTY;
  assert (dp->dn_stat.st_size == 0);
  assert (dp->dn->u.dir.dotdot == pdp->dn);
  return 0;
}

int
diskfs_dirempty (struct node *dp, struct protid *cred)
{
  return dp->dn->u.dir.entries == 0;
}

error_t
diskfs_get_directs (struct node *dp, int entry, int n,
		    char **data, u_int *datacnt,
		    vm_size_t bufsiz, int *amt)
{
  struct tmpfs_dirent *d;
  struct dirent *entp;
  int i;

  assert (offsetof (struct tmpfs_dirent, name)
	  >= offsetof (struct dirent, d_name));

  if (bufsiz == 0)
    bufsiz = dp->dn_stat.st_size;
  if (bufsiz > *datacnt)
    {
      *data = mmap (0, bufsiz, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (*data == MAP_FAILED)
	return ENOMEM;
    }

  entp = (struct dirent *) *data;
  entp->d_fileno = dp->dn_stat.st_ino;
  entp->d_type = DT_DIR;
  entp->d_namlen = 1;
  entp->d_name[0] = '.';
  entp->d_name[1] = '\0';
  entp->d_reclen = (&entp->d_name[2] - (char *) entp + 7) & ~7;
  entp = (void *) entp + entp->d_reclen;
  entp->d_fileno = (ino_t) dp->dn->u.dir.dotdot;
  entp->d_type = DT_DIR;
  entp->d_namlen = 2;
  entp->d_name[0] = '.';
  entp->d_name[1] = '.';
  entp->d_name[2] = '\0';
  entp->d_reclen = (&entp->d_name[3] - (char *) entp + 7) & ~7;
  entp = (void *) entp + entp->d_reclen;

  d = dp->dn->u.dir.entries;
  for (i = 2; i < entry && d != 0; ++i)
    d = d->next;

  for (i = 2; d != 0; d = d->next)
    {
      if ((char *) entp - *data >= bufsiz || (n >= 0 && ++i > n))
	break;
      entp->d_fileno = (ino_t) d->dn;
      entp->d_type = DT_UNKNOWN;
      entp->d_namlen = d->namelen;
      memcpy (entp->d_name, d->name, d->namelen + 1);
      entp->d_reclen = ((&entp->d_name[d->namelen + 1] - (char *) entp + 7)
			& ~7);
      entp = (void *) entp + entp->d_reclen;
    }

  *datacnt = (char *) entp - *data;
  *amt = i;

  return 0;
}


struct dirstat
{
  struct tmpfs_dirent **prevp;
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
      struct node *ddnp = 0;
      error_t err;

      assert (np != 0);
      if (dddn == 0)		/* root directory */
	return EAGAIN;

      err = diskfs_cached_lookup ((int) dddn, &ddnp);
      switch (type)
	{
	case LOOKUP|SPEC_DOTDOT:
	  diskfs_nput (dp);
	default:
	  if (!err)
	    {
	      diskfs_nref (ddnp);
	      mutex_lock (&ddnp->lock);
	    }
	case REMOVE|SPEC_DOTDOT:
	case RENAME|SPEC_DOTDOT:
	  *np = ddnp;
	}
      return err;
    }

  for (d = *(prevp = &dp->dn->u.dir.entries); d != 0;
       d = *(prevp = &d->next))
    if (d->namelen == namelen && !memcmp (d->name, name, namelen))
      {
	ds->prevp = prevp;
	return diskfs_cached_lookup ((ino_t) d->dn, np);
      }

  ds->prevp = prevp;
  return ENOENT;
}


error_t
diskfs_direnter_hard (struct node *dp, const char *name,
		      struct node *np, struct dirstat *ds,
		      struct protid *cred)
{
  const size_t namelen = strlen (name);
  const size_t entsize = offsetof (struct tmpfs_dirent, name) + namelen + 1;
  struct tmpfs_dirent *new;

  if (round_page (tmpfs_space_used + entsize) > tmpfs_page_limit)
    return ENOSPC;

  new = malloc (entsize);
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
  (*ds->prevp)->dn = np->dn;
  return 0;
}

error_t
diskfs_dirremove_hard (struct node *dp, struct dirstat *ds)
{
  struct tmpfs_dirent *d = *ds->prevp;
  const size_t entsize = &d->name[d->namelen + 1] - (char *) d;

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
