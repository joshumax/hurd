/* XDR frobbing and lower level routines for NFS client
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

/* Convert an NFS mode (TYPE and MODE) to a Hurd mode and return it. */
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

/* Convert a Hurd mode to an NFS mode */
int
hurd_mode_to_nfs_mode (mode_t mode)
{
  /* This function is used only for chmod; just trim the bits that NFS
     doesn't support. */
  return mode & 07777;
}


/* Each of the functions on this page copies its second arg to *P,
   converting it to XDR representation along the way.  They then
   return the address after the copied value. */

/* Encode an NFS file handle. */
int *
xdr_encode_fhandle (int *p, void *fhandle)
{
  bcopy (fhandle, p, NFS2_FHSIZE);
  return p + INTSIZE (NFS2_FHSIZE);
}

/* Encode uninterpreted bytes. */
int *
xdr_encode_data (int *p, char *data, size_t len)
{
  int nints = INTSIZE (len);
  
  p[nints] = 0;
  *p++ = htonl (len);
  bcopy (data, p, len);
  return p + nints;
}

/* Encode a C string. */
int *
xdr_encode_string (int *p, char *string)
{
  return xdr_encode_data (p, string, strlen (string));
}
  
/* Encode a MODE into an otherwise empty sattr. */
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

/* Encode UID and GID into an otherwise empty sattr. */
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

/* Encode a file size into an otherwise empty sattr. */
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

/* Encode ATIME and MTIME into an otherwise empty sattr. */
int *
xdr_encode_sattr_times (int *p, struct timespec *atime, struct timespec *mtime)
{
  *p++ = -1;			/* mode */
  *p++ = -1;			/* uid */
  *p++ = -1;			/* gid */
  *p++ = -1;			/* size */
  *p++ = htonl (atime->tv_sec);
  *p++ = htonl (atime->tv_nsec * 1000);
  *p++ = htonl (mtime->tv_sec);
  *p++ = htonl (mtime->tv_nsec * 1000);
  return p;
}

/* Encode MODE and a size of 0 into an otherwise empty sattr. */
int *
xdr_encode_create_state (int *p, 
			 mode_t mode)
{
  *p++ = htonl (hurd_mode_to_nfs_mode (mode));
  *p++ = -1;			/* uid */
  *p++ = -1;			/* gid */
  *p++ = 0;			/* size */
  *p++ = -1;			/* atime sec */
  *p++ = -1;			/* atime usec */
  *p++ = -1;			/* mtime sec */
  *p++ = -1;			/* mtime usec */
  return p;
}

/* Encode ST into an sattr. */
int *
xdr_encode_sattr_stat (int *p,
		       struct stat *st)
{
  *p++ = htonl (st->st_mode);
  *p++ = htonl (st->st_uid);
  *p++ = htonl (st->st_gid);
  *p++ = htonl (st->st_size);
  *p++ = htonl (st->st_atime);
  *p++ = htonl (st->st_atime_usec);
  *p++ = htonl (st->st_mtime);
  *p++ = htonl (st->st_mtime_usec);
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
  st->st_fsid = ntohl (*p++);
  st->st_ino = ntohl (*p++);
  st->st_atime = ntohl (*p++);
  st->st_atime_usec = ntohl (*p++);
  st->st_mtime = ntohl (*p++);
  st->st_mtime_usec = ntohl (*p++);
  st->st_ctime = ntohl (*p++);
  st->st_ctime_usec = ntohl (*p++);

  return p;

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


/* Set up an RPC for procedure RPC_PROC for talking to the NFS server.
   Allocate storage with malloc and point *BUFP at it; caller must
   free this when done.  Initialize RPC credential information with
   information from CRED (identifying the user making this call; -1
   means superuser), NP (identifying the node we are operating on), and
   SECOND_GID (specifying another GID the server might be interested
   in).  Allocate at least LEN bytes of space for bulk data in
   addition to the normal amount for an RPC. */
int *
nfs_initialize_rpc (int rpc_proc, struct netcred *cred,
		    size_t len, void **bufp, struct node *np,
		    uid_t second_gid)
{
  uid_t uid;
  uid_t gid;
  error_t err;
  
  /* Use heuristics to figure out what ids to present to the server.
     Don't lie, but adjust ids as necessary to secure the desired result. */ 

  if (cred == (struct netcred *) -1)
    {
      uid = gid = 0;
      second_gid = -1;
    }
  else if (cred
	   && (cred->nuids || cred->ngids))
    {
      if (cred_has_uid (cred, 0))
	{
	  err = netfs_validate_stat (np, 0);
	  uid = 0;
	  gid = err ? -2 : 0;
	  if (err)
	    printf ("NFS warning, internal stat failure\n");
	}
      else
	{
	  if (cred->nuids == 0)
	    uid = -2;
	  else if (cred->nuids == 1)
	    uid = cred->uids[0];
	  else
	    {
	      err = netfs_validate_stat (np, 0);
	      if (err)
		{
		  uid = cred->uids[0];
		  printf ("NFS warning, internal stat failure\n");
		}
	      else
		{
		  if (cred_has_uid (cred, np->nn_stat.st_uid))
		    uid = np->nn_stat.st_uid;
		  else
		    uid = cred->uids[0];
		}
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
	      err = netfs_validate_stat (np, 0);
	      if (err)
		{
		  gid = cred->gids[0];
		  printf ("NFS warning, internal stat failure\n");
		}
	      else
		{
		  if (cred_has_gid (cred, np->nn_stat.st_gid))
		    gid = np->nn_stat.st_gid;
		  else
		    gid = cred->gids[0];
		}		  
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

/* ERROR is an NFS error code; return the correspending Hurd error. */
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
	    /* These indicate bugs in the client, so EGRATUITOUS is right. */
	    return EGRATUITOUS;
	  }
    }
}
