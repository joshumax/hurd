/* XDR packing and unpacking in nfsd
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

#include <sys/stat.h>
#include <sys/statfs.h>
#include <string.h>
#include "nfsd.h"

/* Return the address of the next thing after the credential at P. */
int *
skip_cred (int *p)
{
  int size;
  
  p++;				/* TYPE */
  size = ntohl (*p++);
  return p + INTSIZE (size);
}

/* Any better ideas? */
static int
hurd_mode_to_nfs_mode (mode_t m)
{
  return m & 0x177777;
}

static int
hurd_mode_to_nfs_type (mode_t m)
{
  switch (m & S_IFMT)
    {
    case S_IFDIR:
      return NFDIR;

    case S_IFCHR:
      return NFCHR;
      
    case S_IFBLK:
      return NFBLK;
      
    case S_IFREG:
      return NFREG;
      
    case S_IFLNK:
      return NFLNK;
      
    case S_IFSOCK:
      return NFSOCK;
      
    case S_IFIFO:
      return NFFIFO;
      
    default:
      return NFNON;
    }
}

/* Encode ST into P and return the next thing to come after it. */
int *
encode_fattr (int *p, struct stat *st)
{
  *p++ = htonl (hurd_mode_to_nfs_type (st->st_mode));
  *p++ = htonl (hurd_mode_to_nfs_mode (st->st_mode));
  *p++ = htonl (st->st_nlink);
  *p++ = htonl (st->st_uid);
  *p++ = htonl (st->st_gid);
  *p++ = htonl (st->st_size);
  *p++ = htonl (st->st_blksize);
  *p++ = htonl (st->st_rdev);
  *p++ = htonl (st->st_blocks);
  return p;
}

/* Decode P into NAME and return the next thing to come after it. */
int *
decode_name (int *p, char **name)
{
  int len;
  
  len = ntohl (*p++);
  *name = malloc (len + 1);
  bcopy (p, *name, len);
  (*name)[len] = '\0';
  return p + INTSIZE (len);
}

/* Encode HANDLE into P and return the next thing to come after it. */
int *
encode_fhandle (int *p, char *handle)
{
  bcopy (handle, p, NFS_FHSIZE);
  return p + INTSIZE (NFS_FHSIZE);
}

/* Encode STRING into P and return the next thing to come after it. */
int *
encode_string (int *p, char *string)
{
  return encode_data (p, string, strlen (string));
}

/* Encode DATA into P and return the next thing to come after it. */
int *
encode_data (int *p, char *data, size_t len)
{
  int nints = INTSIZE (len);
  
  p[nints] = 0;
  *p++ = htonl (len);
  bcopy (data, p, len);
  return p + nints;
}

/* Encode ST into P and return the next thing to come after it. */
int *
encode_statfs (int *p, struct statfs *st)
{
  *p++ = st->f_bsize;
  *p++ = st->f_bsize;
  *p++ = st->f_blocks;
  *p++ = st->f_bfree;
  *p++ = st->f_bavail;
  return p;
}

/* Return an NFS error corresponding to Hurd error ERR. */
int
nfs_error_trans (error_t err)
{
  switch (err)
    {
    case 0:
      return NFS_OK;
      
    case EPERM:
      return NFSERR_PERM;
      
    case ENOENT:
      return NFSERR_NOENT;
      
    case EIO:
    default:
      return NFSERR_IO;
      
    case ENXIO:
      return NFSERR_NXIO;
      
    case EACCES:
      return NFSERR_ACCES;
      
    case EEXIST:
      return NFSERR_EXIST;
      
    case ENODEV:
      return NFSERR_NODEV;
      
    case ENOTDIR:
      return NFSERR_NOTDIR;
      
    case EISDIR:
      return NFSERR_ISDIR;
      
    case E2BIG:
      return NFSERR_FBIG;
      
    case ENOSPC:
      return NFSERR_NOSPC;
      
    case EROFS:
      return NFSERR_ROFS;
      
    case ENAMETOOLONG:
      return NFSERR_NAMETOOLONG;
      
    case ENOTEMPTY:
      return NFSERR_NOTEMPTY;
      
    case EDQUOT:
      return NFSERR_DQUOT;
      
    case ESTALE:
      return NFSERR_STALE;
    }
}      
      
      
     
