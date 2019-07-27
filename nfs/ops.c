/* ops.c - Libnetfs callbacks for node operations in NFS client.
   Copyright (C) 1994,95,96,97,99,2002,2011 Free Software Foundation, Inc.

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

#include "nfs.h"
#include <hurd/netfs.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stddef.h>
#include <dirent.h>
#include <unistd.h>
#include <maptime.h>
#include <sys/sysmacros.h>

/* We have fresh stat information for NP; the file attribute (fattr)
   structure is at P.  Update our entry.  Return the address of the next
   int after the fattr structure.  */
int *
register_fresh_stat (struct node *np, int *p)
{
  int *ret;

  ret = xdr_decode_fattr (p, &np->nn_stat);
  np->nn->stat_updated = mapped_time->seconds;

  switch (np->nn->dtrans)
    {
    case NOT_POSSIBLE:
    case POSSIBLE:
      break;

    case SYMLINK:
      np->nn_stat.st_size = strlen (np->nn->transarg.name);
      np->nn_stat.st_mode = ((np->nn_stat.st_mode & ~S_IFMT) | S_IFLNK);
      break;

    case CHRDEV:
      np->nn_stat.st_rdev = np->nn->transarg.indexes;
      np->nn_stat.st_mode = ((np->nn_stat.st_mode & ~S_IFMT) | S_IFCHR);
      break;

    case BLKDEV:
      np->nn_stat.st_rdev = np->nn->transarg.indexes;
      np->nn_stat.st_mode = ((np->nn_stat.st_mode & ~S_IFMT) | S_IFBLK);
      break;

    case FIFO:
      np->nn_stat.st_mode = ((np->nn_stat.st_mode & ~S_IFMT) | S_IFIFO);
      break;

    case SOCK:
      np->nn_stat.st_mode = ((np->nn_stat.st_mode & ~S_IFMT) | S_IFSOCK);
      break;
    }

  np->nn_stat.st_fsid = getpid ();
  np->nn_stat.st_fstype = FSTYPE_NFS;
  np->nn_stat.st_gen = 0;
  np->nn_stat.st_author = np->nn_stat.st_uid;
  np->nn_stat.st_flags = 0;
  np->nn_translated = np->nn_stat.st_mode & S_IFMT;

  return ret;
}

/* Handle returned wcc information for various calls.  In protocol
   version 2, this is just register_fresh_stat.  In version 3, it
   checks to see if stat information is present too.  If this follows
   an operation that we expect has modified the attributes, MOD should
   be set.  (This unpacks the post_op_attr XDR type.)  */
int *
process_returned_stat (struct node *np, int *p, int mod)
{
  if (protocol_version == 2)
    return register_fresh_stat (np, p);
  else
    {
      int attrs_exist;

      attrs_exist = ntohl (*p);
      p++;
      if (attrs_exist)
	p = register_fresh_stat (np, p);
      else if (mod)
	/* We know that our values are now wrong */
	np->nn->stat_updated = 0;
      return p;
    }
}


/* Handle returned wcc information for various calls.  In protocol
   version 2, this is just register_fresh_stat.  In version 3, it does
   the wcc_data interpretation too.  If this follows an operation that
   we expect has modified the attributes, MOD should be set.
   (This unpacks the wcc_data XDR type.)  */
int *
process_wcc_stat (struct node *np, int *p, int mod)
{
  if (protocol_version == 2)
    return register_fresh_stat (np, p);
  else
    {
      int attrs_exist;

      /* First the pre_op_attr */
      attrs_exist = ntohl (*p);
      p++;
      if (attrs_exist)
	{
	  /* Just skip them for now */
	  p += 2 * sizeof (int); /* size */
	  p += 2 * sizeof (int); /* mtime */
	  p += 2 * sizeof (int); /* atime */
	}

      /* Now the post_op_attr */
      return process_returned_stat (np, p, mod);
    }
}


/* Implement the netfs_validate_stat callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_validate_stat (struct node *np, struct iouser *cred)
{
  int *p;
  void *rpcbuf;
  error_t err;

  if (mapped_time->seconds - np->nn->stat_updated < stat_timeout)
    return 0;

  p = nfs_initialize_rpc (NFSPROC_GETATTR (protocol_version),
			  (struct iouser *) -1, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
    }
  if (!err)
    register_fresh_stat (np, p);

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_chown callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_chown (struct iouser *cred, struct node *np,
		     uid_t uid, gid_t gid)
{
  int *p;
  void *rpcbuf;
  error_t err;

  p = nfs_initialize_rpc (NFSPROC_SETATTR (protocol_version),
			  cred, 0, &rpcbuf, np, gid);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_sattr_ids (p, uid, gid);
  if (protocol_version == 3)
    *(p++) = 0;			/* guard_check == 0 */

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (!err || protocol_version == 3)
	p = process_wcc_stat (np, p, !err);
    }

  free (rpcbuf);

  return err;
}

/* Implement the netfs_attempt_chauthor callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_chauthor (struct iouser *cred, struct node *rp,
			uid_t author)
{
  return EOPNOTSUPP;
}

/* Implement the netfs_attempt_chmod callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_chmod (struct iouser *cred, struct node *np,
		     mode_t mode)
{
  int *p;
  void *rpcbuf;
  error_t err;

  if ((mode & S_IFMT) != 0)
    {
      err = netfs_validate_stat (np, cred);
      if (err)
	return err;

      /* Has the file type changed? (e.g. from symlink to
	 directory).  */
      if ((mode & S_IFMT) != (np->nn_stat.st_mode & S_IFMT))
	{
	  char *f = 0;

	  if (np->nn->dtrans == NOT_POSSIBLE)
	    return EOPNOTSUPP;

	  if (np->nn->dtrans == SYMLINK)
	    f = np->nn->transarg.name;

	  switch (mode & S_IFMT)
	    {
	    default:
	      return EOPNOTSUPP;

	    case S_IFIFO:
	      np->nn->dtrans = FIFO;
	      np->nn->stat_updated = 0;
	      break;

	    case S_IFSOCK:
	      np->nn->dtrans = SOCK;
	      np->nn->stat_updated = 0;
	    }
	  free (f);
	  return 0;
	}
    }

  p = nfs_initialize_rpc (NFSPROC_SETATTR (protocol_version),
			  cred, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_sattr_mode (p, mode);
  if (protocol_version == 3)
    *(p++) = 0;			/* guard check == 0 */

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (!err || protocol_version == 3)
	p = process_wcc_stat (np, p, !err);
    }

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_chflags callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_chflags (struct iouser *cred, struct node *np,
		       int flags)
{
  return EOPNOTSUPP;
}

/* Implement the netfs_attempt_utimes callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_utimes (struct iouser *cred, struct node *np,
		      struct timespec *atime, struct timespec *mtime)
{
  int *p;
  void *rpcbuf;
  error_t err;

  if (!atime && !mtime)
    return 0; /* nothing to update */

  /* XXX For version 3 we can actually do this right, but we don't
     just yet. */

  p = nfs_initialize_rpc (NFSPROC_SETATTR (protocol_version),
			  cred, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_sattr_times (p, atime, mtime);
  if (protocol_version == 3)
    *(p++) = 0;			/* guard check == 0 */

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (!err || protocol_version == 3)
	p = process_wcc_stat (np, p, !err);
    }

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_set_size callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_set_size (struct iouser *cred, struct node *np,
			off_t size)
{
  int *p;
  void *rpcbuf;
  error_t err;

  p = nfs_initialize_rpc (NFSPROC_SETATTR (protocol_version),
			  cred, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_sattr_size (p, size);
  if (protocol_version == 3)
    *(p++) = 0;			/* guard_check == 0 */

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (!err || protocol_version == 3)
	p = process_wcc_stat (np, p, !err);
    }

  /* If we got EACCES, but the user has the file open for writing,
     then the NFS protocol has screwed us.  There's nothing we can do,
     except in the important case of opens with
     O_TRUNC|O_CREAT|O_WRONLY|O_EXCL where the new mode does not allow
     writing.  RCS, for example, uses this to create lock files.  So permit
     cases where the O_TRUNC isn't doing anything to succeed if the user
     does have the file open for writing.  */
  if (err == EACCES)
    {
      int error = netfs_validate_stat (np, cred);
      if (!error && np->nn_stat.st_size == size)
	err = 0;
    }

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_statfs callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_statfs (struct iouser *cred, struct node *np,
		      struct statfs *st)
{
  int *p;
  void *rpcbuf;
  error_t err;

  p = nfs_initialize_rpc (NFS2PROC_STATFS, cred, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
    }

  if (!err)
    {
      p++;			/* skip IOSIZE field */
      st->f_bsize = ntohl (*p);
      p++;
      st->f_blocks = ntohl (*p);
      p++;
      st->f_bfree = ntohl (*p);
      p++;
      st->f_bavail = ntohl (*p);
      p++;
      st->f_type = FSTYPE_NFS;
      st->f_files = 0;
      st->f_ffree = 0;
      st->f_fsid = getpid ();
      st->f_namelen = 0;
    }

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_sync callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_sync (struct iouser *cred, struct node *np, int wait)
{
  /* We are already completely synchronous. */
  return 0;
}

/* Implement the netfs_attempt_syncfs callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  return 0;
}

/* Implement the netfs_attempt_read callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_read (struct iouser *cred, struct node *np,
		    off_t offset, size_t *len, void *data)
{
  int *p;
  void *rpcbuf;
  size_t trans_len;
  error_t err;
  size_t amt, thisamt;
  int eof;

  for (amt = *len; amt;)
    {
      thisamt = amt;
      if (thisamt > read_size)
	thisamt = read_size;

      p = nfs_initialize_rpc (NFSPROC_READ (protocol_version),
			      cred, 0, &rpcbuf, np, -1);
      if (! p)
        return errno;

      p = xdr_encode_fhandle (p, &np->nn->handle);
      *(p++) = htonl (offset);
      *(p++) = htonl (thisamt);
      if (protocol_version == 2)
	*(p++) = 0;

      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	{
	  err = nfs_error_trans (ntohl (*p));
	  p++;

	  if (!err || protocol_version == 3)
	    p = process_returned_stat (np, p, !err);

	  if (err)
	    {
	      free (rpcbuf);
	      return err;
	    }

	  trans_len = ntohl (*p);
	  p++;
	  if (trans_len > thisamt)
	    trans_len = thisamt;	/* ??? */

	  if (protocol_version == 3)
	    {
	      eof = ntohl (*p);
	      p++;
	    }
	  else
	    eof = (trans_len < thisamt);

	  memcpy (data, p, trans_len);
	  free (rpcbuf);

	  data += trans_len;
	  offset += trans_len;
	  amt -= trans_len;

	  if (eof)
	    {
	      *len -= amt;
	      return 0;
	    }
	}
    }
  return 0;
}

/* Implement the netfs_attempt_write callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_write (struct iouser *cred, struct node *np,
		     off_t offset, size_t *len, void *data)
{
  int *p;
  void *rpcbuf;
  error_t err;
  size_t amt, thisamt;
  size_t count;

  for (amt = *len; amt;)
    {
      thisamt = amt;
      if (thisamt > write_size)
	thisamt = write_size;

      p = nfs_initialize_rpc (NFSPROC_WRITE (protocol_version),
			      cred, thisamt, &rpcbuf, np, -1);
      if (! p)
        return errno;

      p = xdr_encode_fhandle (p, &np->nn->handle);
      if (protocol_version == 2)
	*(p++) = 0;
      *(p++) = htonl (offset);
      if (protocol_version == 2)
	*(p++) = 0;
      if (protocol_version == 3)
	*(p++) = htonl (FILE_SYNC);
      p = xdr_encode_data (p, data, thisamt);

      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	{
	  err = nfs_error_trans (ntohl (*p));
	  p++;
	  if (!err || protocol_version == 3)
	    p = process_wcc_stat (np, p, !err);
	  if (!err)
	    {
	      if (protocol_version == 3)
		{
		  count = ntohl (*p);
		  p++;
		  p++;		/* ignore COMMITTED */
		  /* ignore verf for now */
		  p += NFS3_WRITEVERFSIZE / sizeof (int);
		}
	      else
		/* assume it wrote the whole thing */
		count = thisamt;

	      amt -= count;
	      data += count;
	      offset += count;
	    }
	}

      free (rpcbuf);

      if (err == EINTR && amt != *len)
	{
	  *len -= amt;
	  return 0;
	}

      if (err)
	{
	  *len = 0;
	  return err;
	}
    }
  return 0;
}

/* See if NAME exists in DIR for CRED.  If so, return EEXIST.  */
error_t
verify_nonexistent (struct iouser *cred, struct node *dir,
		    char *name)
{
  int *p;
  void *rpcbuf;
  error_t err;

  /* Don't use the lookup cache for this; we want a full sync to
     get as close to real exclusive create behavior as possible. */

  assert_backtrace (protocol_version == 2);

  p = nfs_initialize_rpc (NFSPROC_LOOKUP (protocol_version),
			  cred, 0, &rpcbuf, dir, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &dir->nn->handle);
  p = xdr_encode_string (p, name);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
    }

  free (rpcbuf);

  if (!err)
    return EEXIST;
  else
    return 0;
}

/* Implement the netfs_attempt_lookup callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_lookup (struct iouser *cred, struct node *np,
		      char *name, struct node **newnp)
{
  int *p;
  void *rpcbuf;
  error_t err;
  char dirhandle[NFS3_FHSIZE];
  size_t dirlen;

  /* Check the cache first. */
  *newnp = check_lookup_cache (np, name);
  if (*newnp)
    {
      if (*newnp == (struct node *) -1)
	{
	  *newnp = 0;
	  return ENOENT;
	}
      else
	return 0;
    }

  p = nfs_initialize_rpc (NFSPROC_LOOKUP (protocol_version),
			  cred, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_string (p, name);

  /* Remember the directory handle for later cache use. */

  dirlen = np->nn->handle.size;
  memcpy (dirhandle, np->nn->handle.data, dirlen);

  pthread_mutex_unlock (&np->lock);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (!err)
	{
	  p = xdr_decode_fhandle (p, newnp);
	  p = process_returned_stat (*newnp, p, 1);
	}
      if (err)
	*newnp = 0;
      if (protocol_version == 3)
	{
	  if (*newnp)
	    pthread_mutex_unlock (&(*newnp)->lock);
	  pthread_mutex_lock (&np->lock);
	  p = process_returned_stat (np, p, 0); /* XXX Do we have to lock np? */
	  pthread_mutex_unlock (&np->lock);
	  if (*newnp)
	    pthread_mutex_lock (&(*newnp)->lock);
	}
    }
  else
    *newnp = 0;

  /* Notify the cache of the hit or miss. */
  enter_lookup_cache (dirhandle, dirlen, *newnp, name);

  free (rpcbuf);

  return err;
}

/* Implement the netfs_attempt_mkdir callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_mkdir (struct iouser *cred, struct node *np,
		     char *name, mode_t mode)
{
  int *p;
  void *rpcbuf;
  error_t err;
  uid_t owner;
  struct node *newnp;

  if (cred->uids->num)
    owner = cred->uids->ids[0];
  else
    {
      err = netfs_validate_stat (np, cred);
      owner = err ? 0 : np->nn_stat.st_uid;
      mode &= ~S_ISUID;
    }

  purge_lookup_cache (np, name, strlen (name));

  p = nfs_initialize_rpc (NFSPROC_MKDIR (protocol_version),
			  cred, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_string (p, name);
  p = xdr_encode_create_state (p, mode, owner);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
    }

  if (!err)
    {
      p = xdr_decode_fhandle (p, &newnp);
      p = process_returned_stat (newnp, p, 1);

      /* Did we set the owner correctly?  If not, try, but ignore failures. */
      if (!netfs_validate_stat (newnp, (struct iouser *) -1)
          && newnp->nn_stat.st_uid != owner)
        netfs_attempt_chown ((struct iouser *) -1, newnp, owner,
			     newnp->nn_stat.st_gid);

      /* We don't actually return this. */
      netfs_nput (newnp);
    }

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_rmdir callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_rmdir (struct iouser *cred, struct node *np,
		     char *name)
{
  int *p;
  void *rpcbuf;
  error_t err;

  /* Should we do the same sort of thing here as with attempt_unlink? */

  purge_lookup_cache (np, name, strlen (name));

  p = nfs_initialize_rpc (NFSPROC_RMDIR (protocol_version),
			  cred, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_string (p, name);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (protocol_version == 3)
	p = process_wcc_stat (np, p, !err);
    }

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_link callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_link (struct iouser *cred, struct node *dir,
		    struct node *np, char *name, int excl)
{
  int *p;
  void *rpcbuf;
  error_t err = 0;

  if (!excl)
    {
      /* We have no RPC available that will do an atomic replacement,
	 so we settle for second best; just doing an unlink and ignoring
	 any errors. */
      pthread_mutex_lock (&dir->lock);
      netfs_attempt_unlink (cred, dir, name);
      pthread_mutex_unlock (&dir->lock);
    }

  /* If we have postponed a translator setting on an unlinked node,
     then here's where we set it, by creating the new node instead of
     doing a normal link. */

  switch (np->nn->dtrans)
    {
    case POSSIBLE:
    case NOT_POSSIBLE:
      pthread_mutex_lock (&dir->lock);
      p = nfs_initialize_rpc (NFSPROC_LINK (protocol_version),
			      cred, 0, &rpcbuf, dir, -1);
      if (! p)
	{
          pthread_mutex_unlock (&dir->lock);
          return errno;
	}

      pthread_mutex_unlock (&dir->lock);

      pthread_mutex_lock (&np->lock);
      p = xdr_encode_fhandle (p, &np->nn->handle);
      pthread_mutex_unlock (&np->lock);

      pthread_mutex_lock (&dir->lock);
      purge_lookup_cache (dir, name, strlen (name));

      p = xdr_encode_fhandle (p, &dir->nn->handle);
      p = xdr_encode_string (p, name);

      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	{
	  err = nfs_error_trans (ntohl (*p));
	  p++;
	}
      pthread_mutex_unlock (&dir->lock);

      free (rpcbuf);

      break;

    case SYMLINK:
      pthread_mutex_lock (&dir->lock);
      p = nfs_initialize_rpc (NFSPROC_SYMLINK (protocol_version),
			      cred, 0, &rpcbuf, dir, -1);
      if (! p)
	{
          pthread_mutex_unlock (&dir->lock);
          return errno;
	}

      p = xdr_encode_fhandle (p, &dir->nn->handle);
      pthread_mutex_unlock (&dir->lock);

      p = xdr_encode_string (p, name);

      pthread_mutex_lock (&np->lock);
      err = netfs_validate_stat (np, cred);
      if (err)
	{
	  pthread_mutex_unlock (&np->lock);
	  free (rpcbuf);
	  return err;
	}

      if (protocol_version == 2)
	{
	  p = xdr_encode_string (p, np->nn->transarg.name);
	  p = xdr_encode_sattr_stat (p, &np->nn_stat);
	}
      else
	{
	  p = xdr_encode_sattr_stat (p, &np->nn_stat);
	  p = xdr_encode_string (p, np->nn->transarg.name);
	}
      pthread_mutex_unlock (&np->lock);

      pthread_mutex_lock (&dir->lock);

      purge_lookup_cache (dir, name, strlen (name));
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	{
	  err = nfs_error_trans (ntohl (*p));
	  p++;

	  if (protocol_version == 2 && !err)
	    {
	      free (rpcbuf);

	      /* NFSPROC_SYMLINK stupidly does not pass back an
		 fhandle, so we have to fetch one now. */
	      p = nfs_initialize_rpc (NFSPROC_LOOKUP (protocol_version),
				      cred, 0, &rpcbuf, dir, -1);
	      if (! p)
		{
		  pthread_mutex_unlock (&dir->lock);
		  return errno;
		}
	      p = xdr_encode_fhandle (p, &dir->nn->handle);
	      p = xdr_encode_string (p, name);

	      pthread_mutex_unlock (&dir->lock);

	      err = conduct_rpc (&rpcbuf, &p);
	      if (!err)
		{
		  err = nfs_error_trans (ntohl (*p));
		  p++;
		}
	      if (!err)
		{
		  pthread_mutex_lock (&np->lock);
		  p = recache_handle (p, np);
		  p = process_returned_stat (np, p, 1);
		  pthread_mutex_unlock (&np->lock);
		}
	      if (err)
		err = EGRATUITOUS; /* damn */
	    }
	  else if (protocol_version == 3)
	    {
	      if (!err)
		{
		  pthread_mutex_unlock (&dir->lock);
		  pthread_mutex_lock (&np->lock);
		  p = recache_handle (p, np);
		  p = process_returned_stat (np, p, 1);
		  pthread_mutex_unlock (&np->lock);
		  pthread_mutex_lock (&dir->lock);
		}
	      p = process_wcc_stat (dir, p, !err);
	      pthread_mutex_unlock (&dir->lock);
	    }
	  else
	    pthread_mutex_unlock (&dir->lock);
	}
      else
	pthread_mutex_unlock (&dir->lock);

      free (rpcbuf);
      break;

    case CHRDEV:
    case BLKDEV:
    case FIFO:
    case SOCK:

      if (protocol_version == 2)
	{
	  pthread_mutex_lock (&dir->lock);
	  err = verify_nonexistent (cred, dir, name);
	  if (err)
	    return err;

	  p = nfs_initialize_rpc (NFSPROC_CREATE (protocol_version),
				  cred, 0, &rpcbuf, dir, -1);
          if (! p)
	    {
	      pthread_mutex_unlock (&dir->lock);
              return errno;
            }

	  p = xdr_encode_fhandle (p, &dir->nn->handle);
	  p = xdr_encode_string (p, name);
	  pthread_mutex_unlock (&dir->lock);

	  pthread_mutex_lock (&np->lock);
	  err = netfs_validate_stat (np, cred);
	  if (err)
	    {
	      pthread_mutex_unlock (&np->lock);
	      free (rpcbuf);
	      return err;
	    }

	  p = xdr_encode_sattr_stat (p, &np->nn_stat);
	  pthread_mutex_unlock (&np->lock);

	  pthread_mutex_lock (&dir->lock);
	  purge_lookup_cache (dir, name, strlen (name));
	  pthread_mutex_unlock (&dir->lock); /* XXX Should this really be after the
					_lengthy_ (blocking) conduct_rpc? */
	  err = conduct_rpc (&rpcbuf, &p);
	  if (!err)
	    {
	      err = nfs_error_trans (ntohl (*p));
	      p++;
	    }

	  if (!err)
	    {
	      pthread_mutex_lock (&np->lock);
	      p = recache_handle (p, np);
	      register_fresh_stat (np, p);
	      pthread_mutex_unlock (&np->lock);
	    }

	  free (rpcbuf);
	}
      else /* protocol_version != 2 */
	{
	  pthread_mutex_lock (&dir->lock);
	  p = nfs_initialize_rpc (NFS3PROC_MKNOD, cred, 0, &rpcbuf, dir, -1);
	  if (! p)
	    {
	      pthread_mutex_unlock (&dir->lock);
	      return errno;
	    }
	  p = xdr_encode_fhandle (p, &dir->nn->handle);
	  p = xdr_encode_string (p, name);
	  pthread_mutex_unlock (&dir->lock);

	  pthread_mutex_lock (&np->lock);
	  err = netfs_validate_stat (np, cred);
	  if (err)
	    {
	      pthread_mutex_unlock (&np->lock);
	      free (rpcbuf);
	      return err;
	    }
	  *(p++) = htonl (hurd_mode_to_nfs_type (np->nn_stat.st_mode));
	  p = xdr_encode_sattr_stat (p, &np->nn_stat);
	  if (np->nn->dtrans == BLKDEV || np->nn->dtrans == CHRDEV)
	    {
	      *(p++) = htonl (gnu_dev_major (np->nn_stat.st_rdev));
	      *(p++) = htonl (gnu_dev_minor (np->nn_stat.st_rdev));
	    }
	  pthread_mutex_unlock (&np->lock);

	  purge_lookup_cache (dir, name, strlen (name));
	  err = conduct_rpc (&rpcbuf, &p);
	  if (!err)
	    {
	      err = nfs_error_trans (ntohl (*p));
	      p++;

	      if (!err)
		{
		  pthread_mutex_lock (&np->lock);
		  p = recache_handle (p, np);
		  p = process_returned_stat (np, p, 1);
		  pthread_mutex_unlock (&np->lock);
		}
	      pthread_mutex_lock (&dir->lock);
	      p = process_wcc_stat (dir, p, !err);
	      pthread_mutex_unlock (&dir->lock);
	    }
	  free (rpcbuf);
	}
      break;
    }

  if (err)
    return err;

  pthread_mutex_lock (&np->lock);

  if (np->nn->dtrans == SYMLINK)
    free (np->nn->transarg.name);
  np->nn->dtrans = NOT_POSSIBLE;

  /* If there is a dead-dir tag lying around, it's time to delete it now. */
  if (np->nn->dead_dir)
    {
      struct node *dir = np->nn->dead_dir;
      char *name = np->nn->dead_name;
      np->nn->dead_dir = 0;
      np->nn->dead_name = 0;
      pthread_mutex_unlock (&np->lock);

      pthread_mutex_lock (&dir->lock);
      netfs_attempt_unlink ((struct iouser *)-1, dir, name);
      pthread_mutex_unlock (&dir->lock);
    }
  else
    pthread_mutex_unlock (&np->lock);

  return 0;
}

/* Implement the netfs_attempt_mkfile callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_mkfile (struct iouser *cred, struct node *dir,
		      mode_t mode, struct node **newnp)
{
  error_t err;
  char *name;
  static int n = 0;

  /* This is the best we can do. */

  name = malloc (50);
  if (! name)
    return ENOMEM;

  do
    {
      sprintf (name, ".nfstmpgnu.%d", n++);
      err = netfs_attempt_create_file (cred, dir, name, mode, newnp);
      if (err == EEXIST)
	pthread_mutex_lock (&dir->lock);  /* XXX is this right? does create need this
				     and drop this on error? Doesn't look
				     like it. */
    }
  while (err == EEXIST);

  if (err)
    {
      free (name);
      return err;
    }

  assert_backtrace (!(*newnp)->nn->dead_dir);
  assert_backtrace (!(*newnp)->nn->dead_name);
  netfs_nref (dir);
  (*newnp)->nn->dead_dir = dir;
  (*newnp)->nn->dead_name = name;
  if ((*newnp)->nn->dtrans == NOT_POSSIBLE)
    (*newnp)->nn->dtrans = POSSIBLE;
  return 0;
}

/* Implement the netfs_attempt_create_file callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_create_file (struct iouser *cred, struct node *np,
			   char *name, mode_t mode, struct node **newnp)
{
  int *p;
  void *rpcbuf;
  error_t err;
  uid_t owner;

  if (cred->uids->num)
    owner = cred->uids->ids[0];
  else
    {
      err = netfs_validate_stat (np, cred);
      owner = err ? 0 : np->nn_stat.st_uid;
      mode &= ~S_ISUID;
    }

  /* RFC 1094 says that create is always exclusive.  But Sun doesn't
     actually *implement* the spec.  No, of course not.  So we have to do
     it for them.  */
  if (protocol_version == 2)
    {
      err = verify_nonexistent (cred, np, name);
      if (err)
	{
	  pthread_mutex_unlock (&np->lock);
	  return err;
	}
    }

  purge_lookup_cache (np, name, strlen (name));

  p = nfs_initialize_rpc (NFSPROC_CREATE (protocol_version),
			  cred, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_string (p, name);
  if (protocol_version == 3)
    {
      /* We happen to know this is where the XID is. */
      int verf = *(int *)rpcbuf;

      *(p++) = ntohl (EXCLUSIVE);
      /* 8 byte verf */
      *(p++) = ntohl (verf);
      p++;
    }
  else
    p = xdr_encode_create_state (p, mode, owner);

  err = conduct_rpc (&rpcbuf, &p);

  pthread_mutex_unlock (&np->lock);

  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (!err)
	{
	  p = xdr_decode_fhandle (p, newnp);
	  p = process_returned_stat (*newnp, p, 1);
	}
      if (err)
	*newnp = 0;
      if (protocol_version == 3)
	{
	  if (*newnp)
	    pthread_mutex_unlock (&(*newnp)->lock);
	  pthread_mutex_lock (&np->lock);
	  p = process_wcc_stat (np, p, 1);
	  pthread_mutex_unlock (&np->lock);
	  if (*newnp)
	    pthread_mutex_lock (&(*newnp)->lock);
	}

      if (*newnp && !netfs_validate_stat (*newnp, (struct iouser *) -1)
	  && (*newnp)->nn_stat.st_uid != owner)
	netfs_attempt_chown ((struct iouser *) -1, *newnp, owner, (*newnp)->nn_stat.st_gid);
    }
  else
    *newnp = 0;

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_unlink callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_unlink (struct iouser *cred, struct node *dir,
		      char *name)
{
  int *p;
  void *rpcbuf;
  error_t err;
  struct node *np;

  /* First lookup the node being removed */
  err = netfs_attempt_lookup (cred, dir, name, &np);
  if (err)
    {
      pthread_mutex_lock (&dir->lock);
      return err;
    }

  /* Restore the locks to sanity. */
  pthread_mutex_unlock (&np->lock);
  pthread_mutex_lock (&dir->lock);

  /* Purge the cache of entries for this node, so that we don't
     regard cache-held references as live. */
  purge_lookup_cache_node (np);

  /* See if there are any other users of this node than the
     one we just got; if so, we must give this file another link
     so that when we delete the one we are asked for it doesn't go
     away entirely. */
  struct references result;
  refcounts_references (&np->refcounts, &result);

  if (result.hard > 1)
    {
      char *newname = 0;
      int n = 0;

      pthread_mutex_unlock (&dir->lock);

      newname = malloc (50);
      if (! newname)
	{
	  pthread_mutex_lock (&dir->lock);
	  netfs_nrele (np);         /* XXX Is this the correct thing to do? */
	  return ENOMEM;
	}

      do
	{
	  sprintf (newname, ".nfs%txgnu.%d", (ptrdiff_t) np, n++);
	  err = netfs_attempt_link (cred, dir, np, newname, 1);
	}
      while (err == EEXIST);

      if (err)
	{
	  free (newname);
	  pthread_mutex_lock (&dir->lock);
	  netfs_nrele (np);
	  return err;
	}

      /* Write down what name we gave it; we'll delete this when all
	 our uses vanish.  */
      pthread_mutex_lock (&np->lock);

      if (np->nn->dead_dir)
	netfs_nrele (np->nn->dead_dir);
      netfs_nref (dir);
      np->nn->dead_dir = dir;
      if (np->nn->dead_name)
	free (np->nn->dead_name);
      np->nn->dead_name = newname;
      if (np->nn->dtrans == NOT_POSSIBLE)
	np->nn->dtrans = POSSIBLE;

      netfs_nput (np);
      pthread_mutex_lock (&dir->lock);
    }
  else
    netfs_nrele (np);

  p = nfs_initialize_rpc (NFSPROC_REMOVE (protocol_version),
			  cred, 0, &rpcbuf, dir, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &dir->nn->handle);
  p = xdr_encode_string (p, name);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (protocol_version == 3)
	p = process_wcc_stat (dir, p, !err);
    }

  free (rpcbuf);

  return err;
}

/* Implement the netfs_attempt_rename callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_rename (struct iouser *cred, struct node *fromdir,
		      char *fromname, struct node *todir, char *toname,
		      int excl)
{
  int *p;
  void *rpcbuf;
  error_t err;

  if (excl)
    {
      struct node *np;

      /* Just do a lookup/link/unlink sequence. */

      pthread_mutex_lock (&fromdir->lock);
      err = netfs_attempt_lookup (cred, fromdir, fromname, &np);
      pthread_mutex_unlock (&fromdir->lock);
      if (err)
	return err;

      err = netfs_attempt_link (cred, todir, np, toname, 1);
      netfs_nput (np);
      if (err)
	return err;

      pthread_mutex_lock (&fromdir->lock);
      err = netfs_attempt_unlink (cred, fromdir, fromname);
      pthread_mutex_unlock (&fromdir->lock);

      /* If the unlink failed, then back out the link */
      if (err)
	{
	  pthread_mutex_lock (&todir->lock);
	  netfs_attempt_unlink (cred, todir, toname);
	  pthread_mutex_unlock (&todir->lock);
	  return err;
	}

      return 0;
    }

  pthread_mutex_lock (&fromdir->lock);
  purge_lookup_cache (fromdir, fromname, strlen (fromname));
  p = nfs_initialize_rpc (NFSPROC_RENAME (protocol_version),
			  cred, 0, &rpcbuf, fromdir, -1);
  if (! p)
    {
      pthread_mutex_unlock (&fromdir->lock);
      return errno;
    }

  p = xdr_encode_fhandle (p, &fromdir->nn->handle);
  p = xdr_encode_string (p, fromname);
  pthread_mutex_unlock (&fromdir->lock);

  pthread_mutex_lock (&todir->lock);
  purge_lookup_cache (todir, toname, strlen (toname));
  p = xdr_encode_fhandle (p, &todir->nn->handle);
  p = xdr_encode_string (p, toname);
  pthread_mutex_unlock (&todir->lock);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (protocol_version == 3)  /* XXX Should we add `&& !err' ? */
	{
	  pthread_mutex_lock (&fromdir->lock);
	  p = process_wcc_stat (fromdir, p, !err);
	  p = process_wcc_stat (todir, p, !err);
	}
    }

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_readlink callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_readlink (struct iouser *cred, struct node *np,
			char *buf)
{
  int *p;
  void *rpcbuf;
  error_t err;

  if (np->nn->dtrans == SYMLINK)
    {
      strcpy (buf, np->nn->transarg.name);
      return 0;
    }

  p = nfs_initialize_rpc (NFSPROC_READLINK (protocol_version),
			  cred, 0, &rpcbuf, np, -1);
  if (! p)
    return errno;

  p = xdr_encode_fhandle (p, &np->nn->handle);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    {
      err = nfs_error_trans (ntohl (*p));
      p++;
      if (protocol_version == 3)
	p = process_returned_stat (np, p, 0);
      if (!err)
	p = xdr_decode_string (p, buf);
    }

  free (rpcbuf);
  return err;
}

/* Implement the netfs_check_open_permissions callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_check_open_permissions (struct iouser *cred, struct node *np,
			      int flags, int newnode)
{
  int modes;

  if (newnode || (flags & (O_READ|O_WRITE|O_EXEC)) == 0)
    return 0;

  netfs_report_access (cred, np, &modes);
  if ((flags & (O_READ|O_WRITE|O_EXEC)) == (flags & modes))
    return 0;
  else
    return EACCES;
}

/* Implement the netfs_report_access callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_report_access (struct iouser *cred,
		     struct node *np,
		     int *types)
{
  error_t err;

  err = netfs_validate_stat (np, cred);
  if (err)
    return err;

  if (protocol_version == 2)
    {
      /* Hope the server means the same thing for the bits as we do. */
      *types = 0;
      if (fshelp_access (&np->nn_stat, S_IREAD, cred) == 0)
	*types |= O_READ;
      if (fshelp_access (&np->nn_stat, S_IWRITE, cred) == 0)
	*types |= O_WRITE;
      if (fshelp_access (&np->nn_stat, S_IEXEC, cred) == 0)
	*types |= O_EXEC;
      return 0;
    }
  else
    {
      int *p;
      void *rpcbuf;
      error_t err;
      int ret;
      int write_check, execute_check;

      if (S_ISDIR (np->nn_stat.st_mode))
	{
	  write_check = ACCESS3_MODIFY | ACCESS3_DELETE | ACCESS3_EXTEND;
	  execute_check = ACCESS3_LOOKUP;
	}
      else
	{
	  write_check = ACCESS3_MODIFY;
	  execute_check = ACCESS3_EXECUTE;
	}

      p = nfs_initialize_rpc (NFS3PROC_ACCESS, cred, 0, &rpcbuf, np, -1);
      if (! p)
	return errno;

      p = xdr_encode_fhandle (p, &np->nn->handle);
      *(p++) = htonl (ACCESS3_READ | write_check | execute_check);

      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	{
	  err = nfs_error_trans (ntohl (*p));
	  p++;
	  p = process_returned_stat (np, p, 0);   /* XXX Should this be
						     protected by the
						     if (!err) ? */
	  if (!err)
	    {
	      ret = ntohl (*p);
	      p++;
	      *types = ((ret & ACCESS3_READ ? O_READ : 0)
			| (ret & write_check ? O_WRITE : 0)
			| (ret & execute_check ? O_EXEC : 0));
	    }
	}
      return err;
    }
}

/* These definitions have unfortunate side effects, don't use them,
   clever though they are.  */
#if 0
/* Implement the netfs_check_open_permissions callback as described in
   <hurd/netfs.h>. */
error_t
netfs_check_open_permissions (struct iouser *cred, struct node *np,
			      int flags, int newnode)
{
  char byte;
  error_t err;
  size_t len;

  /* Sun derived nfs client implementations attempt to reproduce the
     server's permission restrictions by hoping they look like Unix,
     and using that to give errors at open time.  Sadly, that loses
     here.  So instead what we do is try and do what the user
     requested; if we can't, then we fail.  Otherwise, we allow the
     open, but acknowledge that the server might still give an error
     later.  (Even with our check, the server can revoke access, thus
     violiting Posix semantics; this means that erring on the side of
     permitting illegal opens won't harm otherwise correct programs,
     because they need to deal with revocation anyway.)  We thus here
     have the advantage of working correctly with servers that allow
     things Unix denies.  */

  if ((flags & O_READ) == 0
      && (flags & O_WRITE) == 0
      && (flags & O_EXEC) == 0)
    return 0;

  err = netfs_validate_stat (np, cred);
  if (err)
    return err;

  switch (np->nn_stat.st_mode & S_IFMT)
    {
      /* Don't know how to check, so return provisional success. */
    default:
      return 0;

    case S_IFREG:
      len = 1;
      err = netfs_attempt_read (cred, np, 0, &len, &byte);
      if (err)
	{
	  if ((flags & O_READ) || (flags & O_EXEC))
	    return err;
	  else
	    /* If we couldn't read a byte, but the user wasn't actually asking
	       for read, then we shouldn't inhibit the open now.  */
	    return 0;
	}

      if (len != 1)
	/* The file is empty; reads are known to be OK, but writes can't be
	   tested, so no matter what, return success.  */
	return 0;

      if (flags & O_WRITE)
	{
	  err = netfs_attempt_write (cred, np, 0, &len, &byte);
	  return err;
	}

      /* Try as we might, we couldn't get the server to bump us, so
	 give (provisional) success. */
      return 0;

    case S_IFDIR:
      if (flags & O_READ)
	{
	  void *rpcbuf;
	  int *p;

	  /* Issue a readdir request; if it fails, then we can
	     return failure.  Otherwise, succeed. */
	  p = nfs_initialize_rpc (NFSPROC_READDIR, cred, 0, &rpcbuf, np, -1);
	  p = xdr_encode_fhandle (p, &np->nn->handle);
	  *(p++) = 0;
	  *(p++) = htonl (50);
	  err = conduct_rpc (&rpcbuf, &p);
	  if (!err)
	    {
	      err = nfs_error_trans (ntohl (*p));
	      p++;
	    }
	  free (rpcbuf);

	  if (err)
	    return err;
	}
      return 0;
    }
}

/* Implement the netfs_report_access callback as described in
   <hurd/netfs.h>.  */
void
netfs_report_access (struct iouser *cred,
		     struct node *np,
		     int *types)
{
  char byte;
  error_t err;
  size_t len;

  /* Much the same logic as netfs_check_open_permissions, except that
     here we err on the side of denying access, and that we always
     have to check everything.  */

  *types = 0;

  len = 1;
  err = netfs_attempt_read (cred, np, 0, &len, &byte);
  if (err)
    return;
  assert_backtrace (len == 1 || len == 0);

  *types |= O_READ | O_EXEC;

  if (len == 1)
    {
      err = netfs_attempt_write (cred, np, 0, &len, &byte);
      if (!err)
	*types |= O_WRITE;
    }
  else
    {
      /* Oh, ugh.  We have to try and write a byte and then truncate
	 back.  God help us if the file becomes unwritable in-between.
	 But because of the use of this function (by setuid programs
	 wanting to see if they should write user's files) we must
	 check this and not just return a presumptive error. */
      byte = 0;
      err = netfs_attempt_write (cred, np, 0, &len, &byte);
      if (!err)
	*types |= O_WRITE;
      netfs_attempt_set_size (cred, np, 0);
    }
}
#endif

/* Fetch the complete contents of DIR into a buffer of directs.  Set
   *BUFP to that buffer.  *BUFP must be freed by the caller when no
   longer needed.  If an error occurs, don't touch *BUFP and return
   the error code.  Set BUFSIZEP to the amount of data used inside
   *BUFP and TOTALENTRIES to the total number of entries copied.  */
static error_t
fetch_directory (struct iouser *cred, struct node *dir,
		 void **bufp, size_t *bufsizep, int *totalentries)
{
  void *buf;
  int cookie;
  int *p;
  void *rpcbuf;
  struct dirent *entry;
  void *bp;
  int bufmalloced;
  int eof;
  error_t err;
  int isnext;

  bufmalloced = read_size;

  buf = malloc (bufmalloced);
  if (! buf)
    return ENOMEM;

  bp = buf;
  cookie = 0;
  eof = 0;
  *totalentries = 0;

  while (!eof)
    {
      /* Fetch new directory entries */
      p = nfs_initialize_rpc (NFSPROC_READDIR (protocol_version),
			      cred, 0, &rpcbuf, dir, -1);
      if (! p)
	{
	  free (buf);
	  return errno;
	}

      p = xdr_encode_fhandle (p, &dir->nn->handle);
      *(p++) = cookie;
      *(p++) = ntohl (read_size);
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	{
	  err = nfs_error_trans (ntohl (*p));
	  p++;
	}
      if (err)
	{
	  free (buf);
	  return err;
	}

      isnext = ntohl (*p);
      p++;

      /* Now copy them one at a time. */
      while (isnext)
	{
	  ino_t fileno;
	  int namlen;
	  int reclen;

	  fileno = ntohl (*p);
	  p++;
	  namlen = ntohl (*p);
	  p++;

	  /* There's a hidden +1 here for the null byte and -1 because d_name
	     has a size of one already in the sizeof.  */
	  reclen = sizeof (struct dirent) + namlen;
	  reclen = (reclen + 3) & ~3; /* make it a multiple of four */

	  /* Expand buffer if necessary */
	  if (bp + reclen > buf + bufmalloced)
	    {
	      char *newbuf;

	      newbuf = realloc (buf, bufmalloced *= 2);
	      assert_backtrace (newbuf);
	      if (newbuf != buf)
		bp = newbuf + (bp - buf);
	      buf = newbuf;
	    }

	  /* Fill in new entry */
	  entry = (struct dirent *) bp;
	  entry->d_fileno = fileno;
	  entry->d_reclen = reclen;
	  entry->d_type = DT_UNKNOWN;
	  entry->d_namlen = namlen;
	  memcpy (entry->d_name, p, namlen);
	  entry->d_name[namlen] = '\0';

	  p += INTSIZE (namlen);
	  bp = bp + entry->d_reclen;

	  ++*totalentries;

	  cookie = *(p++);
	  isnext = ntohl (*p);
	  p++;
	}

      eof = ntohl (*p);
      p++;
      free (rpcbuf);
    }

  /* Return it all to the user */
  *bufp = buf;
  *bufsizep = bufmalloced;
  return 0;
}


/* Implement the netfs_get_directs callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_get_dirents (struct iouser *cred, struct node *np,
		   int entry, int nentries, char **data,
		   mach_msg_type_number_t *datacnt,
		   vm_size_t bufsiz, int *amt)
{
  void *buf = NULL;
  size_t our_bufsiz, allocsize;
  void *bp;
  char *userdp;
  error_t err;
  int totalentries;
  int thisentry;

  err = fetch_directory (cred, np, &buf, &our_bufsiz, &totalentries);
  if (err)
    return err;

  /* Allocate enough space to hold the maximum we might return. */
  if (!bufsiz || bufsiz > our_bufsiz)
    allocsize = round_page (our_bufsiz);
  else
    allocsize = round_page (bufsiz);
  if (allocsize > *datacnt)
    *data = mmap (0, allocsize, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);

  /* Skip ahead to the correct entry. */
  bp = buf;
  for (thisentry = 0; thisentry < entry;)
    {
      struct dirent *entry = (struct dirent *) bp;
      bp += entry->d_reclen;
      thisentry++;
    }

  /* Now copy them one at a time */
  {
    int entries_copied;

    for (entries_copied = 0, userdp = *data;
	 (nentries == -1 || entries_copied < nentries)
	 && (!bufsiz || userdp - *data < bufsiz)
	 && thisentry < totalentries;)
      {
	struct dirent *entry = (struct dirent *) bp;
	memcpy (userdp, bp, entry->d_reclen);
	bp += entry->d_reclen;
	userdp += entry->d_reclen;
	entries_copied++;
	thisentry++;
      }
    *amt = entries_copied;
  }

  free (buf);

  /* If we allocated the buffer ourselves, but didn't use
     all the pages, free the extra. */
  if (allocsize > *datacnt
      && round_page (userdp - *data) < round_page (allocsize))
    munmap ((caddr_t) round_page (userdp),
	    round_page (allocsize) - round_page (userdp - *data));

  *datacnt = userdp - *data;
  return 0;
}


/* Implement the netfs_attempt_mksymlink callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_mksymlink (struct iouser *cred,
			 struct node *np,
			 char *arg)
{
  if (np->nn->dtrans == NOT_POSSIBLE)
    return EOPNOTSUPP;

  if (np->nn->dtrans == SYMLINK)
    free (np->nn->transarg.name);

  np->nn->transarg.name = malloc (strlen (arg) + 1);
  strcpy (np->nn->transarg.name, arg);
  np->nn->dtrans = SYMLINK;
  np->nn->stat_updated = 0;
  return 0;
}

/* Implement the netfs_attempt_mkdev callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_mkdev (struct iouser *cred,
		     struct node *np,
		     mode_t type,
		     dev_t indexes)
{
  if (np->nn->dtrans == NOT_POSSIBLE)
    return EOPNOTSUPP;

  if (np->nn->dtrans == SYMLINK)
    free (np->nn->transarg.name);

  np->nn->transarg.indexes = indexes;
  if (type == S_IFBLK)
    np->nn->dtrans = BLKDEV;
  else
    np->nn->dtrans = CHRDEV;
  np->nn->stat_updated = 0;
  return 0;
}
