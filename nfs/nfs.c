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


/* Count how many four-byte chunks it takss to hold LEN bytes. */
#define INTSIZE(len) (((len)+3)>>2)

/* Each of these functions copies its second arg to *P, converting it
   to XDR representation along the way.  They then return the address after
   the copied value. */

inline int *
xdr_encode_fhandle (int *p, void *fhandle)
{
  bcopy (fhandle, p, NFSV2_FHSIZE);
  return p + INTSIZE (NFSV2_FHSIZE);
}

inline void *
xdr_encode_data (void *p, char *data, size_t len)
{
  int nints = INTLEN (len);
  
  p[nints] = 0;
  *p++ = htonl (len);
  bcopy (string, p, len);
  return p + nints;
}

inline void *
xdr_encode_string (void *p, char *string)
{
  return xdr_encode_data (p, string, strlen (string));
}
  
/* The SATTR calls are different; they each only fill in one 
   or two attributes; the rest get -1. */
inline int *
xdr_encode_sattr_mode (int *p, u_int mode)
{
  *p++ = htonl (sattr->mode);
  *p++ = -1;			/* uid */
  *p++ = -1;			/* gid */
  *p++ = -1;			/* size */
  *p++ = -1;			/* atime secs */
  *p++ = -1;			/* atime usecs */
  *p++ = -1;			/* mtime secs */
  *p++ = -1;			/* mtime usecs */
  return p;
}

inline int *
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

inline int *
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

inline int *
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

inline int *
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
  return p
}


/* Decode *P into a stat structure; return the address of the
 following data. */
int *
xdr_decode_fattr (int *p, struct stat *st)
{
  int type, mode;
  
  type = ntohl (*p++);
  mode = ntohl (*p++);
  st->st_mode = nfsv2mode_to_hurdmode (type, mode);
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
	      netfs_validate_state (np, 0);
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

  return initialize_rpc (program, version, rpc_proc, len, bufp,
			 uid, gid, second_gid);
}

error_t
nfs_error_trans (int error)
{
  switch (error)
    {
    case NFSV2_OK:
      return 0;
      
    case NFSV2_ERR_PERM:
      return EPERM;

    case NFSV2_ERR_NOENT:
      return ENOENT;
		  
    case NFS_ERR_IO:
      return EIO;
		  
    case NFS_ERR_NXIO:
      return ENXIO;
		  
    case NFS_ERR_ACCES:
      return EACCESS;
		  
    case NFS_ERR_EXIST:
      return EEXIST;
		  
    case NFS_ERR_NODEV:
      return ENODEV;
		  
    case NFS_ERR_NOTDIR:
      return ENOTDIR;
		  
    case NFS_ERR_ISDIR:
      return EISDIR;
		  
    case NFS_ERR_FBIG:
      return E2BIG;
      
    case NFS_ERR_NOSPC:
      return ENOSPC;

    case NFS_ERR_ROFS:
      return EROFS;
		  
    case NFS_ERR_NAMETOOLONG:
      return ENAMETOOLONG;
		  
    case NFS_ERR_NOTEMPTY:
      return ENOTEMPTY;
		  
    case NFS_ERR_DQUOT:
      return EDQUOT;
		  
    case NFS_ERR_STALE:
      return ESTALE;
		  
    case NFS_ERR_WFLUSH:
    default:
      return EINVAL;
    }
}

