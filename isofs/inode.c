/*
   Copyright (C) 1997, 1998, 2002, 2007 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG.

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

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "isofs.h"
#include "libdiskfs/fs_S.h"


/* There is no such thing as an inode in this format, all such
   information being recorded in the directory entry.  So we report
   inode numbers as absolute offsets from DISK_IMAGE. We use the directory
   record for symlinks and zero length files, and file_start otherwise.
   Only for hard links to zero length files we get extra inodes.  */

/* Determine if we use file_start or struct dirrect * as node id.  */
int
use_file_start_id (struct dirrect *record, struct rrip_lookup *rr)
{
  /* If it is a symlink or a zero length file, don't use file_start.  */
  if (rr->valid & VALID_SL || isonum_733 (record->size) == 0)
    return 0;

  return 1;
}

/* The user must define this function if she wants to use the node
   cache.  Create and initialize a node.  */
error_t
diskfs_user_make_node (struct node **npp, struct lookup_context *ctx)
{
  error_t err;
  struct node *np;
  struct disknode *dn;

  /* Create the new node.  */
  np = diskfs_make_node_alloc (sizeof *dn);
  if (np == NULL)
    return ENOMEM;

  /* Format specific data for the new node.  */
  dn = diskfs_node_disknode (np);
  dn->fileinfo = 0;
  dn->dr = ctx->dr;
  err = calculate_file_start (ctx->dr, &dn->file_start, &ctx->rr);
  if (err)
    {
      diskfs_nrele (np);
      return err;
    }

  *npp = np;
  return 0;
}

/* Return Epoch-based time from a seven byte according to 9.1.5 */
char *
isodate_915 (char *c, struct timespec *ts)
{
  struct tm tm;
  signed char tz;

  /* Copy into a struct TM. */
  tm.tm_year = *c++;
  tm.tm_mon = *c++ - 1;
  tm.tm_mday = *c++;
  tm.tm_hour = *c++;
  tm.tm_min = *c++;
  tm.tm_sec = *c++;
  tz = *c++;

  tm.tm_isdst = 0;
  ts->tv_sec = timegm (&tm);
  ts->tv_nsec = 0;

  /* Only honor TZ offset if it makes sense */
  if (-48 <= tz && tz <= 52)
    ts->tv_sec -= 15 * 60 * tz;	/* TZ is in fifteen minute chunks */

  return c;
}

/* Return Epoch-based time from a seventeen byte according to 8.4.26.1 */
char *
isodate_84261 (char *c, struct timespec *ts)
{
  struct tm tm;
  int hsec;
  signed char tz;

  sscanf (c, "%4d%2d%2d%2d%2d%2d%2d",
	  &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
	  &tm.tm_hour, &tm.tm_min, &tm.tm_sec,
	  &hsec);

  /* Convert to appropriate units */
  ts->tv_nsec = hsec * 10000000;
  tm.tm_year -= 1900;
  tm.tm_mon--;

  tm.tm_isdst = 0;
  ts->tv_sec = timegm (&tm);

  tz = c[16];

  /* Only honor TZ offset if it makes sense */
  if (-48 <= tz && tz <= 52)
    ts->tv_sec -= 15 * 60 * tz;	/* TZ is in fifteen minute chunks */

  return c + 17;
}

/* Calculate the file start (in store blocks) of the file at RECORD. */
error_t
calculate_file_start (struct dirrect *record, off_t *file_start,
		      struct rrip_lookup *rr)
{
  error_t err;

  if (rr && (rr->valid & VALID_CL))
    {
      *file_start = (void *) rr->realdirent - (void *)disk_image;
      *file_start >>= store->log2_block_size;
    }
  else if (rr && (rr->valid & VALID_PL))
    *file_start = rr->realfilestart;
  else
    {
      if (record == NULL)
        return ENOENT;

      err = diskfs_catch_exception ();
      if (err)
	return err;

      *file_start = ((isonum_733 (record->extent) + record->ext_attr_len)
		     * (logical_block_size / store->block_size));

      diskfs_end_catch_exception ();
    }
  return 0;
}

/* Given RECORD and RR, calculate the cache id.  */
error_t
cache_id (struct dirrect *record, struct rrip_lookup *rr, ino_t *idp)
{
  error_t err;
  off_t file_start;
  err = calculate_file_start (record, &file_start, rr);
  if (err)
    return err;

  if (rr->valid & VALID_CL)
    record = rr->realdirent;

  if (use_file_start_id (record, rr))
    *idp = file_start << store->log2_block_size;
  else
    *idp = (off_t) ((void *) record - (void *) disk_image);
  return 0;
}

/* The user must define this function if she wants to use the node
   cache.  Read stat information out of the on-disk node.  */
error_t
diskfs_user_read_node (struct node *np, struct lookup_context *ctx)
{
  error_t err;
  struct stat *st = &np->dn_stat;
  /* Read stat information from the directory entry at DR and the
     contents of RL. */
  struct dirrect *dr = ctx->dr;
  struct rrip_lookup *rl = &ctx->rr;
  st->st_fstype = FSTYPE_ISO9660;
  st->st_fsid = getpid ();
  st->st_ino = np->cache_id;
  st->st_gen = 0;
  st->st_rdev = 0;

  err = diskfs_catch_exception ();
  if (err)
    return err;

  if (rl->valid & VALID_PX)
    {
      if ((rl->valid & VALID_MD) == 0)
	st->st_mode = rl->mode;
      st->st_nlink = rl->nlink;
      st->st_uid = rl->uid;
      st->st_gid = rl->gid;
    }
  else
    {
      if ((rl->valid & VALID_MD) == 0)
	{
	  /* If there are no periods, it's a directory. */
	  if (((rl->valid & VALID_NM) && !index (rl->name, '.'))
	      || (!(rl->valid & VALID_NM) && !memchr (dr->name, '.',
						      dr->namelen)))
	    st->st_mode = S_IFDIR | 0777;
	  else
	    st->st_mode = S_IFREG | 0666;
	}
      st->st_nlink = 1;
      st->st_uid = 0;
      st->st_gid = 0;
    }

  if (rl->valid & VALID_MD)
    st->st_mode = rl->allmode;

  if (rl->valid & VALID_AU)
    st->st_author = rl->author;
  else
    st->st_author = st->st_gid;

  st->st_size = isonum_733 (dr->size);

  if ((rl->valid & VALID_PN)
      && (S_ISCHR (st->st_mode) || S_ISBLK (st->st_mode)))
    st->st_rdev = rl->rdev;
  else
    st->st_rdev = 0;

  if (dr->ileave)
    /* XXX ??? */
    st->st_size = 0;

  /* Calculate these if we'll need them */
  if (!(rl->valid & VALID_TF)
      || ((rl->tfflags & (TF_CREATION|TF_ACCESS|TF_MODIFY))
	  != (TF_CREATION|TF_ACCESS|TF_MODIFY)))
    {
      struct timespec ts;
      isodate_915 ((char *) dr->date, &ts);
      st->st_ctim = st->st_mtim = st->st_atim = ts;
    }

  /* Override what we have better info for */
  if (rl->valid & VALID_TF)
    {
      if (rl->tfflags & TF_CREATION)
	st->st_ctim = rl->ctime;

      if (rl->tfflags & TF_ACCESS)
	st->st_atim = rl->atime;

      if (rl->tfflags & TF_MODIFY)
	st->st_mtim = rl->mtime;
    }

  st->st_blksize = logical_block_size;
  st->st_blocks = (st->st_size - 1) / 512 + 1;

  if (rl->valid & VALID_FL)
    st->st_flags = rl->flags;
  else
    st->st_flags = 0;

  if (S_ISLNK (st->st_mode))
    {
      if (rl->valid & VALID_SL)
	{
	  np->dn->link_target = rl->target;
	  rl->target = 0;
	  st->st_size = strlen (np->dn->link_target);
	}
      else
	{
	  st->st_mode &= ~S_IFMT;
	  st->st_mode |= S_IFREG;
	}
    }

  if (rl->valid & VALID_TR)
    {
      st->st_mode |= S_IPTRANS;
      np->dn->translen = rl->translen;
      np->dn->translator = rl->trans;
      rl->trans = 0;
    }
  else
    {
      np->dn->translator = 0;
      np->dn->translen = 0;
    }

  diskfs_end_catch_exception ();

  return 0;
}

/* Symlink targets are never stored in files, so always use this. */
static error_t
read_symlink_hook (struct node *np, char *buf)
{
  memcpy (buf, np->dn->link_target, np->dn_stat.st_size + 1);
  return 0;
}
error_t (*diskfs_read_symlink_hook) (struct node *, char *)
     = read_symlink_hook;


/* The last reference to NP has gone away; drop it from the cache
   and clean all state in the dn structure. */
void
diskfs_node_norefs (struct node *np)
{
  if (np->dn->translator)
    free (np->dn->translator);

  assert_backtrace (!np->dn->fileinfo);
  free (np);
}

/* The user must define this function if she wants to use the node
   cache.  The last hard reference to a node has gone away; arrange to
   have all the weak references dropped that can be.  */
void
diskfs_user_try_dropping_softrefs (struct node *np)
{
  drop_pager_softrefs (np);
}

void
diskfs_lost_hardrefs (struct node *np)
{
}

void
diskfs_new_hardrefs (struct node *np)
{
  allow_pager_softrefs (np);
}

error_t
diskfs_truncate (struct node *np, off_t length)
{
  return EROFS;
}

error_t
diskfs_grow (struct node *np, off_t end, struct protid *cred)
{
  return EROFS;
}

error_t
diskfs_set_translator (struct node *np,
		       const char *name, u_int namelen,
		       struct protid *cred)
{
  return EROFS;
}

error_t
diskfs_get_translator (struct node *np, char **namep, u_int *namelen)
{
  return EOPNOTSUPP;
}

void
diskfs_shutdown_soft_ports ()
{
    /* Should initiate termination of internally held pager ports
     (the only things that should be soft) XXX */
}

error_t
diskfs_node_reload (struct node *node)
{
  /* Never necessary on a read-only medium */
  return 0;
}

error_t
diskfs_validate_author_change (struct node *np, uid_t author)
{
  return EROFS;
}

void
diskfs_write_disknode (struct node *np, int wait)
{
}

error_t
diskfs_set_statfs (struct statfs *st)
{
  /* There is no easy way to determine the number of files on an
     ISO 9660 filesystem.  */
  memset (st, 0, sizeof *st);
  st->f_type = FSTYPE_ISO9660;
  st->f_bsize = logical_block_size;
  st->f_blocks = isonum_733 (sblock->vol_sp_size);
  st->f_fsid = getpid ();
  st->f_frsize = logical_block_size;
  return 0;
}

error_t
diskfs_S_file_get_storage_info (struct protid *cred,
				mach_port_t **ports,
				mach_msg_type_name_t *ports_type,
				mach_msg_type_number_t *num_ports,
				int **ints, mach_msg_type_number_t *num_ints,
				off_t **offsets,
				mach_msg_type_number_t *num_offsets,
				data_t *data, mach_msg_type_number_t *data_len)
{
  /* XXX */
  return EOPNOTSUPP;
}

void
diskfs_free_node (struct node *no, mode_t mode)
{
  abort ();
}

error_t
diskfs_alloc_node (struct node *dp, mode_t mode, struct node **np)
{
  return EROFS;
}
