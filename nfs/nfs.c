/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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

#include "nfs.h"

#include <string.h>
#include <netinet/in.h>

/* Convert an NFS mode to a Hurd mode */
mode_t
nfs_mode_to_hurd_mode (int type, int mode)
{
  int hurdmode;
  
  switch (type)
    {
    case NFDIR:
      hurdmode = S_IFDIR;
      break;

    case NFCHR:
      hurdmode = S_IFCHR;
      break;

    case NFBLK:
      hurdmode = S_IFBLK;
      break;

    case NFREG:
    case NFNON:
    case NFBAD:
    default:
      hurdmode = S_IFREG;
      break;
      
    case NFLNK:
      hurdmode = S_IFLNK;
      break;

    case NFSOCK:
      hurdmode = S_IFSOCK;
      break;

    case NFFIFO:
      hurdmode = S_IFIFO;
      break;
    }
  
  hurdmode |= mode & ~NFSMODE_FMT;
  return hurdmode;
}

/* Convert a Hurd mode to an NFS mode */
int
hurd_mode_to_nfs_mode (mode_t mode)
{
  /* This function is used only for chmod; just trim the bits that NFS
     doesn't support. */
  return mode & 07777;
}

/* Each of these functions copies its second arg to *P, converting it
   to XDR representation along the way.  They then return the address after
   the copied value. */

int *
xdr_encode_fhandle (int *p, void *fhandle)
{
  bcopy (fhandle, p, NFS_FHSIZE);
  return p + INTSIZE (NFS_FHSIZE);
}

int *
xdr_encode_data (int *p, char *data, size_t len)
{
  int nints = INTSIZE (len);
  
  p[nints] = 0;
  *p++ = htonl (len);
  bcopy (data, p, len);
  return p + nints;
}

int *
xdr_encode_string (int *p, char *string)
{
  return xdr_encode_data (p, string, strlen (string));
}
  
/* The SATTR calls are different; they each only fill in one 
   or two attributes; the rest get -1. */
int *
xdr_encode_sattr_mode (int *p, mode_t mode)
{
  *p++ = htonl (hurd_mode_to_nfs_mode (mode));
  *p++ = -1;			/* uid */
  *p++ = -1;			/* gid */
  *p++ = -1;			/* size */
  *p++ = -1;			/* atime secs */
  *p++ = -1;			/* atime usecs */
  *p++ = -1;			/* mtime secs */
  *p++ = -1;			/* mtime usecs */
  return p;
}

int *
xdr_encode_sattr_ids (int *p, u_int uid, u_int gid)
{
  *p++ = -1;			/* mode */
  *p++ = htonl (uid);
  *p++ = htonl (gid);
  *p++ = -1;			/* size */
  *p++ = -1;			/* atime secs */
  *p++ = -1;			/* atime usecs */
  *p++ = -1;			/* mtime secs */
  *p++ = -1;			/* mtime usecs */
  return p;
}

int *
xdr_encode_sattr_size (int *p, off_t size)
{
  *p++ = -1;			/* mode */
  *p++ = -1;			/* uid */
  *p++ = -1;			/* gid */
  *p++ = htonl (size);
  *p++ = -1;			/* atime secs */
  *p++ = -1;			/* atime usecs */
  *p++ = -1;			/* mtime secs */
  *p++ = -1;			/* mtime secs */
  return p;
}

int *
xdr_encode_sattr_times (int *p, struct timespec *atime, struct timespec *mtime)
{
  *p++ = -1;			/* mode */
  *p++ = -1;			/* uid */
  *p++ = -1;			/* gid */
  *p++ = -1;			/* size */
  *p++ = htonl (atime->ts_sec);
  *p++ = htonl (atime->ts_nsec * 1000);
  *p++ = htonl (mtime->ts_sec);
  *p++ = htonl (mtime->ts_nsec * 1000);
  return p;
}

int *
xdr_encode_create_state (int *p, 
			 mode_t mode)
{
  *p++ = mode;
  *p++ = -1;			/* uid */
  *p++ = -1;			/* gid */
  *p++ = 0;			/* size */
  *p++ = -1;			/* atime sec */
  *p++ = -1;			/* atime usec */
  *p++ = -1;			/* mtime sec */
  *p++ = -1;			/* mtime usec */
  return p;
}


/* Decode *P into a stat structure; return the address of the
 following data. */
int *
xdr_decode_fattr (int *p, struct stat *st)
{
  int type, mode;
  
  type = ntohl (*p++);
  mode = ntohl (*p++);
  st->st_mode = nfs_mode_to_hurd_mode (type, mode);
  st->st_nlink = ntohl (*p++);
  st->st_uid = ntohl (*p++);
  st->st_gid = ntohl (*p++);
  st->st_size = ntohl (*p++);
  st->st_blksize = ntohl (*p++);
  st->st_rdev = ntohl (*p++);
  st->st_blocks = ntohl (*p++);
  st->st_fsid = ntohl (*p++);	/* surely wrong */
  st->st_ino = ntohl (*p++);
  st->st_atime = ntohl (*p++);
  st->st_atime_usec = ntohl (*p++);
  st->st_mtime = ntohl (*p++);
  st->st_mtime_usec = ntohl (*p++);
  st->st_ctime = ntohl (*p++);
  st->st_ctime_usec = ntohl (*p++);

  st->st_fstype = FSTYPE_NFS;
  st->st_gen = 0;		/* ??? */
  st->st_author = st->st_uid;	/* ??? */
  st->st_flags = 0;		/* ??? */

  return p;

}

/* Decode *P into an fhandle, stored at HANDLE; return the address
   of the following data. */
int *
xdr_decode_fhandle (int *p, void *handle)
{
  bcopy (p, handle, NFS_FHSIZE);
  return p + (NFS_FHSIZE / sizeof (int));
}

/* Decode *P into a string, stored at BUF; return the address
   of the following data. */
int *
xdr_decode_string (int *p, char *buf)
{
  int len;
  
  len = ntohl (*p++);
  bcopy (p, buf, len);
  buf[len] = '\0';
  return p + INTSIZE (len);
}


int *
nfs_initialize_rpc (int rpc_proc, struct netcred *cred,
		    size_t len, void **bufp, struct node *np,
		    uid_t second_gid)
{
  uid_t uid;
  uid_t gid;
  
  if (cred
      && (cred->nuids || cred->ngids))
    {
      if (cred_has_uid (cred, 0))
	{
	  netfs_validate_stat (np, 0);
	  uid = 0;
	  gid = np->nn_stat.st_gid;
	}
      else
	{
	  if (cred->nuids == 0)
	    uid = -2;
	  else if (cred->nuids == 1)
	    uid = cred->uids[0];
	  else
	    {
	      netfs_validate_stat (np, 0);
	      if (cred_has_uid (cred, np->nn_stat.st_uid))
		uid = np->nn_stat.st_uid;
	      else
		uid = cred->uids[0];
	    }

	  if (cred->ngids == 0)
	    {
	      gid = -2;
	      second_gid = -1;
	    }
	  else if (cred->ngids == 1)
	    {
	      gid = cred->gids[0];
	      second_gid = -1;
	    }
	  else
	    {
	      netfs_validate_stat (np, 0);
	      if (cred_has_gid (cred, np->nn_stat.st_gid))
		gid = np->nn_stat.st_gid;
	      else
		gid = cred->gids[0];
	      
	      if (second_gid != -1
		  && !cred_has_gid (cred, second_gid))
		second_gid = -1;
	    }
	}
    }
  else
    uid = gid = second_gid = -1;

  return initialize_rpc (NFS_PROGRAM, NFS_VERSION, rpc_proc, len, bufp,
			 uid, gid, second_gid);
}

error_t
nfs_error_trans (int error)
{
  switch (error)
    {
    case NFS_OK:
      return 0;
      
    case NFSERR_PERM:
      return EPERM;

    case NFSERR_NOENT:
      return ENOENT;
		  
    case NFSERR_IO:
      return EIO;
		  
    case NFSERR_NXIO:
      return ENXIO;
		  
    case NFSERR_ACCES:
      return EACCES;
		  
    case NFSERR_EXIST:
      return EEXIST;
		  
    case NFSERR_NODEV:
      return ENODEV;
		  
    case NFSERR_NOTDIR:
      return ENOTDIR;
		  
    case NFSERR_ISDIR:
      return EISDIR;
		  
    case NFSERR_FBIG:
      return E2BIG;
      
    case NFSERR_NOSPC:
      return ENOSPC;

    case NFSERR_ROFS:
      return EROFS;
		  
    case NFSERR_NAMETOOLONG:
      return ENAMETOOLONG;
		  
    case NFSERR_NOTEMPTY:
      return ENOTEMPTY;
		  
    case NFSERR_DQUOT:
      return EDQUOT;
		  
    case NFSERR_STALE:
      return ESTALE;
		  
    case NFSERR_WFLUSH:
    default:
      return EINVAL;
    }
}

