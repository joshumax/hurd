/* nfs.c - XDR frobbing and lower level routines for NFS client.

   Copyright (C) 1995, 1996, 1997, 1999, 2002, 2007
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

#include "nfs.h"

#include <string.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/sysmacros.h>

/* Convert an NFS mode (TYPE and MODE) to a Hurd mode and return
   it.  */
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
      hurdmode = S_IFREG;
      break;

    case NFLNK:
      hurdmode = S_IFLNK;
      break;

    case NFSOCK:
      hurdmode = S_IFSOCK;
      break;

    default:
      if (protocol_version == 2)
	switch (type)
	  {
	  case NF2NON:
	  case NF2BAD:
	  default:
	    hurdmode = S_IFREG;
	    break;

	  case NF2FIFO:
	    hurdmode = S_IFIFO;
	    break;
	  }
      else
	switch (type)
	  {
	  case NF3FIFO:
	    hurdmode = S_IFIFO;
	    break;

	  default:
	    hurdmode = S_IFREG;
	    break;
	  }
      break;
    }

  hurdmode |= mode & ~NFSMODE_FMT;
  return hurdmode;
}

/* Convert a Hurd mode to an NFS mode.  */
int
hurd_mode_to_nfs_mode (mode_t mode)
{
  /* This function is used only for chmod; just trim the bits that NFS
     doesn't support. */
  return mode & 07777;
}

/* Convert a Hurd mode to an NFS type.  */
int
hurd_mode_to_nfs_type (mode_t mode)
{
  switch (mode & S_IFMT)
    {
    case S_IFDIR:
      return NFDIR;

    case S_IFCHR:
    default:
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
      return protocol_version == 2 ? NF2FIFO : NF3FIFO;
    }
}



/* Each of the functions on this page copies its second arg to *P,
   converting it to XDR representation along the way.  They then
   return the address after the copied value.  */

/* Encode an NFS file handle.  */
int *
xdr_encode_fhandle (int *p, struct fhandle *fhandle)
{
  if (protocol_version == 2)
    {
      memcpy (p, fhandle->data, NFS2_FHSIZE);
      return p + INTSIZE (NFS2_FHSIZE);
    }
  else
    return xdr_encode_data (p, fhandle->data, fhandle->size);
}

/* Encode uninterpreted bytes.  */
int *
xdr_encode_data (int *p, char *data, size_t len)
{
  int nints = INTSIZE (len);

  p[nints] = 0;
  *(p++) = htonl (len);
  memcpy (p, data, len);
  return p + nints;
}

/* Encode a 64 bit integer.  */
int *
xdr_encode_64bit (int *p, long long n)
{
  *(p++) = htonl (n & 0xffffffff00000000LL >> 32);
  *(p++) = htonl (n & 0xffffffff);
  return p;
}

/* Encode a C string.  */
int *
xdr_encode_string (int *p, char *string)
{
  return xdr_encode_data (p, string, strlen (string));
}

/* Encode a MODE into an otherwise empty sattr.  */
int *
xdr_encode_sattr_mode (int *p, mode_t mode)
{
  if (protocol_version == 2)
    {
      *(p++) = htonl (hurd_mode_to_nfs_mode (mode));
      *(p++) = -1;			/* uid */
      *(p++) = -1;			/* gid */
      *(p++) = -1;			/* size */
      *(p++) = -1;			/* atime secs */
      *(p++) = -1;			/* atime usecs */
      *(p++) = -1;			/* mtime secs */
      *(p++) = -1;			/* mtime usecs */
    }
  else
    {
      *(p++) = htonl (1);		/* set mode */
      *(p++) = htonl (hurd_mode_to_nfs_mode (mode));
      *(p++) = 0;			/* no uid */
      *(p++) = 0;			/* no gid */
      *(p++) = 0;			/* no size */
      *(p++) = DONT_CHANGE;	/* no atime */
      *(p++) = DONT_CHANGE;	/* no mtime */
    }
  return p;
}

/* Encode UID and GID into an otherwise empty sattr.  */
int *
xdr_encode_sattr_ids (int *p, u_int uid, u_int gid)
{
  if (protocol_version == 2)
    {
      *(p++) = -1;			/* mode */
      *(p++) = htonl (uid);
      *(p++) = htonl (gid);
      *(p++) = -1;			/* size */
      *(p++) = -1;			/* atime secs */
      *(p++) = -1;			/* atime usecs */
      *(p++) = -1;			/* mtime secs */
      *(p++) = -1;			/* mtime usecs */
    }
  else
    {
      *(p++) = 0;			/* no mode */
      *(p++) = htonl (1);		/* set uid */
      *(p++) = htonl (uid);
      *(p++) = htonl (1);		/* set gid */
      *(p++) = htonl (gid);
      *(p++) = 0;			/* no size */
      *(p++) = DONT_CHANGE;	/* no atime */
      *(p++) = DONT_CHANGE;	/* no mtime */
    }
  return p;
}

/* Encode a file size into an otherwise empty sattr.  */
int *
xdr_encode_sattr_size (int *p, off_t size)
{
  if (protocol_version == 2)
    {
      *(p++) = -1;			/* mode */
      *(p++) = -1;			/* uid */
      *(p++) = -1;			/* gid */
      *(p++) = htonl (size);
      *(p++) = -1;			/* atime secs */
      *(p++) = -1;			/* atime usecs */
      *(p++) = -1;			/* mtime secs */
      *(p++) = -1;			/* mtime secs */
    }
  else
    {
      *(p++) = 0;			/* no mode */
      *(p++) = 0;			/* no uid */
      *(p++) = 0;			/* no gid */
      *(p++) = htonl (1);		/* size */
      p = xdr_encode_64bit (p, size);
      *(p++) = DONT_CHANGE;	/* no atime */
      *(p++) = DONT_CHANGE;	/* no mtime */
    }
  return p;
}

/* Encode ATIME and MTIME into an otherwise empty sattr.  */
int *
xdr_encode_sattr_times (int *p, struct timespec *atime, struct timespec *mtime)
{
  if (protocol_version == 2)
    {
      *(p++) = -1;			/* mode */
      *(p++) = -1;			/* uid */
      *(p++) = -1;			/* gid */
      *(p++) = -1;			/* size */
      if (atime)
       {
        *(p++) = htonl (atime->tv_sec);
        *(p++) = htonl (atime->tv_nsec / 1000);
       }
      else
       {
        *(p++) = -1; /* no atime */
        *(p++) = -1;
       }
      if (mtime)
       {
        *(p++) = htonl (mtime->tv_sec);
        *(p++) = htonl (mtime->tv_nsec / 1000);
       }
      else
       {
         *(p++) = -1; /* no mtime */
         *(p++) = -1;
       }
    }
  else
    {
      *(p++) = 0;			/* no mode */
      *(p++) = 0;			/* no uid */
      *(p++) = 0;			/* no gid */
      *(p++) = 0;			/* no size */
      if (atime)
        {
          *(p++) = htonl (SET_TO_CLIENT_TIME); /* atime */
          *(p++) = htonl (atime->tv_sec);
          *(p++) = htonl (atime->tv_nsec);
        }
      else
        *(p++) = DONT_CHANGE;	/* no atime */
      if (mtime)
        {
          *(p++) = htonl (SET_TO_CLIENT_TIME); /* mtime */
          *(p++) = htonl (mtime->tv_sec);
          *(p++) = htonl (mtime->tv_nsec);
        }
      else
        *(p++) = DONT_CHANGE;	/* no mtime */
    }
  return p;
}

/* Encode MODE, a size of zero, and the specified owner into an
   otherwise empty sattr.  */
int *
xdr_encode_create_state (int *p,
			 mode_t mode,
			 uid_t owner)
{
  if (protocol_version == 2)
    {
      *(p++) = htonl (hurd_mode_to_nfs_mode (mode));
      *(p++) = htonl (owner);	/* uid */
      *(p++) = -1;		/* gid */
      *(p++) = 0;			/* size */
      *(p++) = -1;		/* atime sec */
      *(p++) = -1;		/* atime usec */
      *(p++) = -1;		/* mtime sec */
      *(p++) = -1;		/* mtime usec */
    }
  else
    {
      *(p++) = htonl (1);		/* mode */
      *(p++) = htonl (hurd_mode_to_nfs_mode (mode));
      *(p++) = htonl (1);		/* set uid */
      *(p++) = htonl (owner);
      *(p++) = 0;			/* no gid */
      *(p++) = htonl (1);		/* set size */
      p = xdr_encode_64bit (p, 0);
      *(p++) = htonl (SET_TO_SERVER_TIME); /* atime */
      *(p++) = htonl (SET_TO_SERVER_TIME); /* mtime */
    }
  return p;
}

/* Encode ST into an sattr.  */
int *
xdr_encode_sattr_stat (int *p,
		       struct stat *st)
{
  if (protocol_version == 2)
    {
      *(p++) = htonl (hurd_mode_to_nfs_mode (st->st_mode));
      *(p++) = htonl (st->st_uid);
      *(p++) = htonl (st->st_gid);
      *(p++) = htonl (st->st_size);
      *(p++) = htonl (st->st_atim.tv_sec);
      *(p++) = htonl (st->st_atim.tv_nsec / 1000);
      *(p++) = htonl (st->st_mtim.tv_sec);
      *(p++) = htonl (st->st_mtim.tv_nsec / 1000);
    }
  else
    {
      *(p++) = htonl (1);		/* set mode */
      *(p++) = htonl (hurd_mode_to_nfs_mode (st->st_mode));
      *(p++) = htonl (1);		/* set uid */
      *(p++) = htonl (st->st_uid);
      *(p++) = htonl (1);		/* set gid */
      *(p++) = htonl (st->st_gid);
      *(p++) = htonl (1);		/* set size */
      p = xdr_encode_64bit (p, st->st_size);
      *(p++) = htonl (SET_TO_CLIENT_TIME); /* set atime */
      *(p++) = htonl (st->st_atim.tv_sec);
      *(p++) = htonl (st->st_atim.tv_nsec);
      *(p++) = htonl (SET_TO_CLIENT_TIME); /* set mtime */
      *(p++) = htonl (st->st_mtim.tv_sec);
      *(p++) = htonl (st->st_mtim.tv_nsec);
    }
  return p;
}


/* Decode *P into a long long; return the address of the following
   data.  */
int *
xdr_decode_64bit (int *p, long long *n)
{
  long long high, low;
  high = ntohl (*p);
  p++;
  low = ntohl (*p);
  p++;
  *n = ((high & 0xffffffff) << 32) | (low & 0xffffffff);
  return p;
}

/* Decode *P into an fhandle and look up the associated node.  Return
   the address of the following data.  */
int *
xdr_decode_fhandle (int *p, struct node **npp)
{
  struct fhandle handle;

  if (protocol_version == 2)
    handle.size = NFS2_FHSIZE;
  else
    {
      handle.size = ntohl (*p);
      p++;
    }
  memcpy (&handle.data, p, handle.size);
  /* Enter into cache.  */
  lookup_fhandle (&handle, npp);
  return p + handle.size / sizeof (int);
}

/* Decode *P into a stat structure; return the address of the
   following data.  */
int *
xdr_decode_fattr (int *p, struct stat *st)
{
  int type, mode;

  type = ntohl (*p);
  p++;
  mode = ntohl (*p);
  p++;
  st->st_mode = nfs_mode_to_hurd_mode (type, mode);
  st->st_nlink = ntohl (*p);
  p++;
  st->st_uid = ntohl (*p);
  p++;
  st->st_gid = ntohl (*p);
  p++;
  if (protocol_version == 2)
    {
      st->st_size = ntohl (*p);
      p++;
      st->st_blksize = ntohl (*p);
      p++;
      st->st_rdev = ntohl (*p);
      p++;
      st->st_blocks = ntohl (*p);
      p++;
    }
  else
    {
      long long size;
      int major, minor;
      p = xdr_decode_64bit (p, &size);
      st->st_size = size;
      p = xdr_decode_64bit (p, &size);
      st->st_blocks = size / 512;
      st->st_blksize = read_size < write_size ? read_size : write_size;
      major = ntohl (*p);
      p++;
      minor = ntohl (*p);
      p++;
      st->st_rdev = gnu_dev_makedev (major, minor);
    }
  st->st_fsid = ntohl (*p);
  p++;
  st->st_ino = ntohl (*p);
  p++;
  st->st_atim.tv_sec = ntohl (*p);
  p++;
  st->st_atim.tv_nsec = ntohl (*p);
  p++;
  st->st_mtim.tv_sec = ntohl (*p);
  p++;
  st->st_mtim.tv_nsec = ntohl (*p);
  p++;
  st->st_ctim.tv_sec = ntohl (*p);
  p++;
  st->st_ctim.tv_nsec = ntohl (*p);
  p++;

  if (protocol_version < 3)
    {
      st->st_atim.tv_nsec *= 1000;
      st->st_mtim.tv_nsec *= 1000;
      st->st_ctim.tv_nsec *= 1000;
    }

  return p;

}

/* Decode *P into a string, stored at BUF; return the address
   of the following data.  */
int *
xdr_decode_string (int *p, char *buf)
{
  int len;

  len = ntohl (*p);
  p++;
  memcpy (buf, p, len);
  buf[len] = '\0';
  return p + INTSIZE (len);
}


/* Set up an RPC for procedure RPC_PROC for talking to the NFS server.
   Allocate storage with malloc and point *BUFP at it; caller must
   free this when done.  Initialize RPC credential information with
   information from CRED (identifying the user making this call; -1
   means superuser), NP (identifying the node we are operating on), and
   SECOND_GID (specifying another GID the server might be interested
   in).  Allocate at least LEN bytes of space for bulk data in
   addition to the normal amount for an RPC.  */
int *
nfs_initialize_rpc (int rpc_proc, struct iouser *cred,
		    size_t len, void **bufp, struct node *np,
		    uid_t second_gid)
{
  uid_t uid;
  uid_t gid;
  error_t err;

  /* Use heuristics to figure out what ids to present to the server.
     Don't lie, but adjust ids as necessary to secure the desired
     result.  */

  if (cred == (struct iouser *) -1)
    {
      uid = gid = 0;
      second_gid = -1;
    }
  else if (cred
	   && (cred->uids->num || cred->gids->num))
    {
      if (idvec_contains (cred->uids, 0))
	{
	  err = netfs_validate_stat (np, 0);
	  uid = 0;
	  gid = err ? -2 : 0;
	  if (err)
	    printf ("NFS warning, internal stat failure\n");
	}
      else
	{
	  if (cred->uids->num == 0)
	    uid = -2;
	  else if (cred->uids->num == 1)
	    uid = cred->uids->ids[0];
	  else
	    {
	      err = netfs_validate_stat (np, 0);
	      if (err)
		{
		  uid = cred->uids->ids[0];
		  printf ("NFS warning, internal stat failure\n");
		}
	      else
		{
		  if (idvec_contains (cred->uids, np->nn_stat.st_uid))
		    uid = np->nn_stat.st_uid;
		  else
		    uid = cred->uids->ids[0];
		}
	    }

	  if (cred->gids->num == 0)
	    {
	      gid = -2;
	      second_gid = -1;
	    }
	  else if (cred->gids->num == 1)
	    {
	      gid = cred->gids->ids[0];
	      second_gid = -1;
	    }
	  else
	    {
	      err = netfs_validate_stat (np, 0);
	      if (err)
		{
		  gid = cred->gids->ids[0];
		  printf ("NFS warning, internal stat failure\n");
		}
	      else
		{
		  if (idvec_contains (cred->gids, np->nn_stat.st_gid))
		    gid = np->nn_stat.st_gid;
		  else
		    gid = cred->gids->ids[0];
		}
	      if (second_gid != -1
		  && !idvec_contains (cred->gids, second_gid))
		second_gid = -1;
	    }
	}
    }
  else
    uid = gid = second_gid = -1;

  return initialize_rpc (NFS_PROGRAM, NFS_VERSION, rpc_proc, len, bufp,
			 uid, gid, second_gid);
}

/* ERROR is an NFS error code; return the correspending Hurd
   error.  */
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
      /* Not known in v3, but we just give EINVAL for unknown errors
	 so it's the same. */
      return EINVAL;

    default:
      if (protocol_version == 2)
	return EINVAL;
      else
	switch (error)
	  {
	  case NFSERR_XDEV:
	    return EXDEV;

	  case NFSERR_INVAL:
	  case NFSERR_REMOTE:	/* not sure about this one */
	  default:
	    return EINVAL;

	  case NFSERR_MLINK:
	    return EMLINK;

	  case NFSERR_NOTSUPP:
	  case NFSERR_BADTYPE:
	    return EOPNOTSUPP;

	  case NFSERR_SERVERFAULT:
	    return EIO;

	  case NFSERR_BADHANDLE:
	  case NFSERR_NOT_SYNC:
	  case NFSERR_BAD_COOKIE:
	  case NFSERR_TOOSMALL:
	  case NFSERR_JUKEBOX:	/* ??? */
	    /* These indicate bugs in the client, so EGRATUITOUS is right.  */
	    return EGRATUITOUS;
	  }
    }
}
