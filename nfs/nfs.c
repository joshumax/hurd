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

/* Create, initialize, and return a buffer suitable for sending an RPC
   of type RPC_PROC for the user identified in CRED.  For types READ,
   WRITE, READLINK, and READDIR, parm LEN is the amount of data the
   user desires.  Return the address of where RPC args should go; fill
   *pp with the address of the allocated memory.  The RPC will be used
   to operate on node NP.  If this is a chown call, then set
   SECOND_GID to the target of the call, else make it -1.  */
int *
nfs_initialize_rpc (int rpc_proc, struct netcred *cred,
		    size_t len, void **pp, struct node *np,
		    uid_t second_gid)
{
  void *buf = malloc (len + 1024);
  int *p = buf, *lenaddr;
  uid_t chosen_uid, chosen_gid;
  
  /* RPC header */
  *p++ = ntohl (generate_xid);
  *p++ = ntohl (RPCV2_CALL);
  *p++ = ntohl (RPCV2_VERSION);
  *p++ = ntohl (NFSV2_RPC_PROGRAM);
  *p++ = ntohl (NFSV2_RPC_VERSION);
  *p++ = ntohl (rpc_proc);
  

  /* CRED field */
  if (cred
      && (cred->nuids || cred->ngids))
    {
      *p++ = htonl (RPCV2_AUTH_UNIX);
      lenaddr = p++;
      *p++ = htonl (mapped_time->seconds);
      p = xdr_encode_string (p, hostname);
      
      if (cred_has_uid (cred, 0))
	{
	  netfs_validate_stat (np, 0);
	  chosen_uid = 0;
	  chosen_gid = nn->nn_stat.st_gid;
	}
      else
	{
	  if (cred->nuids == 0)
	    chosen_uid = -2;	/* Eeeewwww */
	  else if (cred->nuids == 1)
	    chosen_uid = cred->uids[0];
	  else
	    {
	      netfs_validate_stat (np, 0);
	      if (cred_has_uid (cred, nn->nn_stat.st_uid))
		chosen_uid = nn->nn_stat.st_uid;
	      else
		chosen_uid = cred->uids[0];
	    }

	  if (cred->ngids == 0)
	    {
	      chosen_gid = -2;
	      second_gid = -1;
	    }
	  else if (cred->ngids == 1)
	    {
	      chosen_gid = cred->gids[0];
	      second_gid = -1;
	    }
	  else
	    {
	      netfs_validate_stat (np, 0);

	      if (cred_has_gid (cred, nn->nn_stat.st_gid))
		chosen_gid = nn->nn_stat.st_gid;
	      else
		chosen_gid = cred->gids[0];
	  
	      if ((second_gid >= 0)
		  && (!cred_has_gid (cred, second_gid)))
		second_gid = -1;
	    }
	}
      
      *p++ = htonl (chosen_uid);
      *p++ = htonl (chosen_gid);
      if (second_gid == -1)
	{
	  *p++ = htonl (1);
	  *p++ = htonl (second_gid);
	}
      else
	*p++ = 0;

      *lenaddr = htonl ((p - (nlenaddr + 1)) * sizeof (int));
    }
  else
    {
      *p++ = htonl (RPC_AUTH_NULL);
      *p++ = 0;
    }
        
  /* VERF field */
  *p++ = htonl (RPC_AUTH_NULL);
  *p++ = 0;
  
  *pp = buf;
  return p;
}

  
  
