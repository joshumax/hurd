/* Libnetfs callbacks for node operations in NFS client
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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
#include <dirent.h>
#include <unistd.h>

/* We have fresh stat information for NP; the fattr structure is at 
   P.  Update our entry.  Return the address of the next int after
   the fattr structure.  */
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
       
  return ret;
}

/* Implement the netfs_validate_stat callback as described in
   <hurd/netfs.h>. */
error_t
netfs_validate_stat (struct node *np, struct netcred *cred)
{
  int *p;
  void *rpcbuf;
  error_t err;
  
  if (mapped_time->seconds - np->nn->stat_updated < stat_timeout)
    return 0;

  p = nfs_initialize_rpc (NFSPROC_GETATTR, (struct netcred *) -1,
			  0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  if (!err)
    register_fresh_stat (np, p);

  np->istranslated = 0;

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_chown callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_chown (struct netcred *cred, struct node *np,
		     uid_t uid, gid_t gid)
{
  int *p;
  void *rpcbuf;
  error_t err;
    
  p = nfs_initialize_rpc (NFSPROC_SETATTR, cred, 0, &rpcbuf, np, gid);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_sattr_ids (p, uid, gid);
  
  err = conduct_rpc (&rpcbuf, &p);

  if (!err)
    register_fresh_stat (np, p);

  free (rpcbuf);
  
  return err;
}

/* Implement the netfs_attempt_chauthor callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_chauthor (struct netcred *cred, struct node *rp,
			uid_t author)
{
  return EOPNOTSUPP;
}

/* Implement the netfs_attempt_chmod callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_chmod (struct netcred *cred, struct node *np,
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
	  if (f)
	    free (f);
	  return 0;
	}
    }

  p = nfs_initialize_rpc (NFSPROC_SETATTR, cred, 0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_sattr_mode (p, mode);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  if (!err)
    register_fresh_stat (np, p);

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_chflags callback as described in
   <hurd/netfs.h>. */
error_t 
netfs_attempt_chflags (struct netcred *cred, struct node *np,
		       int flags)
{
  return EOPNOTSUPP;
}

/* Implement the netfs_attempt_utimes callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_utimes (struct netcred *cred, struct node *np,
		      struct timespec *atime, struct timespec *mtime)
{
  int *p;
  void *rpcbuf;
  error_t err;
    
  p = nfs_initialize_rpc (NFSPROC_SETATTR, cred, 0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_sattr_times (p, atime, mtime);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  if (!err)
    register_fresh_stat (np, p);

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_set_size callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_set_size (struct netcred *cred, struct node *np,
			off_t size)
{
  int *p;
  void *rpcbuf;
  error_t err;
  
  p = nfs_initialize_rpc (NFSPROC_SETATTR, cred, 0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_sattr_size (p, size);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  if (!err)
    register_fresh_stat (np, p);

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_statfs callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_statfs (struct netcred *cred, struct node *np,
		      struct statfs *st)
{
  int *p;
  void *rpcbuf;
  error_t err;
    
  p = nfs_initialize_rpc (NFSPROC_SETATTR, cred, 0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  if (!err)
    {
      long iosize, bsize;
      iosize = *p++;
      bsize = *p++;
      st->f_bsize = iosize;
      st->f_blocks = ((*p++) * bsize) / iosize;
      st->f_bfree = ((*p++) * bsize) / iosize;
      st->f_bavail = ((*p++) * bsize) / iosize;
      
      st->f_type = FSTYPE_NFS;
      st->f_files = 0;
      st->f_ffree = 0;
      st->f_fsid = 0;	/* XXX wrong  */
      st->f_namelen = 0;
    }
  
  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_sync callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_sync (struct netcred *cred, struct node *np, int wait)
{
  /* We are already completely synchronous. */
  return 0;
}

/* Implement the netfs_attempt_syncfs callback as described in
   <hurd/netfs.h>.  */
error_t
netfs_attempt_syncfs (struct netcred *cred, int wait)
{
  return 0;
}

/* Implement the netfs_attempt_read callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_read (struct netcred *cred, struct node *np,
		    off_t offset, size_t *len, void *data)
{
  int *p;
  void *rpcbuf;
  size_t trans_len;
  error_t err;
  size_t amt, thisamt;
  
  for (amt = *len; amt;)
    {
      thisamt = amt;
      if (thisamt > read_size)
	thisamt = read_size;

      p = nfs_initialize_rpc (NFSPROC_READ, cred, 0, &rpcbuf, np, -1);
      p = xdr_encode_fhandle (p, &np->nn->handle);
      *p++ = htonl (offset);
      *p++ = htonl (thisamt);
      *p++ = 0;
  
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	err = nfs_error_trans (ntohl (*p++));
  
      if (err)
	{
	  free (rpcbuf);
	  return err;
	}
      
      p = register_fresh_stat (np, p);
      
      trans_len = ntohl (*p++);
      if (trans_len > thisamt)
	trans_len = thisamt;	/* ??? */
      
      bcopy (p, data, trans_len);
      free (rpcbuf);

      data += trans_len;
      offset += trans_len;
      amt -= trans_len;
      
      /* If we got a short count, that means we're all done */
      if (trans_len < thisamt)
	{
	  *len -= amt;
	  return 0;
	}
    }
  return 0;
}
	  
/* Implement the netfs_attempt_write callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_write (struct netcred *cred, struct node *np,
		     off_t offset, size_t *len, void *data)
{
  int *p;
  void *rpcbuf;
  error_t err;
  size_t amt, thisamt;
  
  for (amt = *len; amt;)
    {
      thisamt = amt;
      if (thisamt > write_size)
	thisamt = write_size;
      
      p = nfs_initialize_rpc (NFSPROC_WRITE, cred, thisamt, &rpcbuf, np, -1);
      p = xdr_encode_fhandle (p, &np->nn->handle);
      *p++ = 0;
      *p++ = htonl (offset);
      *p++ = 0;
      p = xdr_encode_data (p, data, thisamt);
  
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	err = nfs_error_trans (ntohl (*p++));
  
      if (err)
	{
	  *len = 0;
	  free (rpcbuf);
	  return err;
	}
      register_fresh_stat (np, p);
      free (rpcbuf);
      amt -= thisamt;
      data += thisamt;
      offset += thisamt;
    }
  return 0;
}

/* Implement the netfs_attempt_lookup callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_lookup (struct netcred *cred, struct node *np,
		      char *name, struct node **newnp)
{
  int *p;
  void *rpcbuf;
  error_t err;
  
  p = nfs_initialize_rpc (NFSPROC_LOOKUP, cred, 0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_string (p, name);
  
  mutex_unlock (&np->lock);

  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  if (!err)
    {
      *newnp = lookup_fhandle (p);
      p += NFS_FHSIZE / sizeof (int);
      register_fresh_stat (*newnp, p);
    }
  else
    *newnp = 0;
      
  free (rpcbuf);
  
  return err;
}

/* Implement the netfs_attempt_mkdir callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_mkdir (struct netcred *cred, struct node *np,
		     char *name, mode_t mode)
{
  int *p;
  void *rpcbuf;
  error_t err;
  
  p = nfs_initialize_rpc (NFSPROC_MKDIR, cred, 0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_string (p, name);
  p = xdr_encode_create_state (p, mode);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  /* Ignore returned information */
  /* XXX should probably cache it */

  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_rmdir callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_rmdir (struct netcred *cred, struct node *np,
		     char *name)
{
  int *p;
  void *rpcbuf;
  error_t err;
  
  /* Should we do the same sort of thing here as with attempt_unlink? */

  p = nfs_initialize_rpc (NFSPROC_RMDIR, cred, 0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_string (p, name);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_link callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_link (struct netcred *cred, struct node *dir,
		    struct node *np, char *name)
{
  int *p;
  void *rpcbuf;
  error_t err = 0;
  
  /* If we have postponed a translator setting on an unlinked node,
     then here's where we set it, by creating the new node instead of
     doing a normal link. */

  switch (np->nn->dtrans)
    {
    case POSSIBLE:
    case NOT_POSSIBLE:
      mutex_lock (&dir->lock);
      p = nfs_initialize_rpc (NFSPROC_LINK, cred, 0, &rpcbuf, dir, -1);
      mutex_unlock (&dir->lock);
      
      mutex_lock (&np->lock);
      p = xdr_encode_fhandle (p, &np->nn->handle);
      mutex_unlock (&np->lock);
  
      mutex_lock (&dir->lock);
      p = xdr_encode_fhandle (p, &dir->nn->handle);
      p = xdr_encode_string (p, name);
  
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	err = nfs_error_trans (ntohl (*p++));
      mutex_unlock (&dir->lock);
      
      free (rpcbuf);

      break;

    case SYMLINK:
      mutex_lock (&dir->lock);
      p = nfs_initialize_rpc (NFSPROC_SYMLINK, cred, 0, &rpcbuf, dir, -1);
      mutex_unlock (&dir->lock);
      
      p = xdr_encode_string (p, name);

      mutex_lock (&np->lock);
      err = netfs_validate_stat (np, cred);
      if (err)
	{
	  mutex_unlock (&np->lock);
	  free (rpcbuf);
	  return err;
	}
      
      p = xdr_encode_string (p, np->nn->transarg.name);
      p = xdr_encode_sattr_stat (p, &np->nn_stat);
      mutex_unlock (&np->lock);

      mutex_lock (&dir->lock);
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	err = nfs_error_trans (ntohl (*p++));
      
      if (!err)
	{
	  /* NFSPROC_SYMLINK stupidly does not pass back an
	     fhandle, so we have to fetch one now. */
	  p = nfs_initialize_rpc (NFSPROC_LOOKUP, cred, 0, &rpcbuf, dir, -1);
	  p = xdr_encode_fhandle (p, &dir->nn->handle);
	  p = xdr_encode_string (p, name);
	  
	  err = conduct_rpc (&rpcbuf, &p);
	  if (!err)
	    err = nfs_error_trans (ntohl (*p++));

	  mutex_unlock (&dir->lock);

	  if (err)
	    err = EGRATUITOUS;	/* damn */
	  else
	    {
	      mutex_lock (&np->lock);
	      recache_handle (np, p);
	      p += NFS_FHSIZE / sizeof (int);
	      register_fresh_stat (np, p);
	      mutex_unlock (&np->lock);
	    }
	}
      else
	mutex_unlock (&dir->lock);
      break;
      
    case CHRDEV:
    case BLKDEV:
    case FIFO:
    case SOCK:
      mutex_lock (&dir->lock);
      p = nfs_initialize_rpc (NFSPROC_CREATE, cred, 0, &rpcbuf, dir, -1);
      p = xdr_encode_fhandle (p, &dir->nn->handle);
      p = xdr_encode_string (p, name);
      mutex_unlock (&dir->lock);

      mutex_lock (&np->lock);
      err = netfs_validate_stat (np, cred);
      if (err)
	{
	  mutex_unlock (&np->lock);
	  free (rpcbuf);
	  return err;
	}
      
      p = xdr_encode_sattr_stat (p, &np->nn_stat);
      mutex_unlock (&np->lock);

      mutex_lock (&dir->lock);
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	err = nfs_error_trans (ntohl (*p++));
      mutex_unlock (&dir->lock);
      
      mutex_lock (&np->lock);
      recache_handle (np, p);
      p += NFS_FHSIZE / sizeof (int);
      register_fresh_stat (np, p);
      mutex_unlock (&np->lock);

      break;
    }

  if (err)
    return err;
  
  mutex_lock (&np->lock);

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
      mutex_unlock (&np->lock);
      
      mutex_lock (&dir->lock);
      netfs_attempt_unlink ((struct netcred *)-1, dir, name);
      mutex_unlock (&dir->lock);
    }
  else
    mutex_unlock (&np->lock);
  
  return 0;
}

/* Implement the netfs_attempt_mkfile callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_mkfile (struct netcred *cred, struct node *dir,
		      mode_t mode, struct node **newnp)
{
  error_t err;
  char *name;
  static int n = 0;

  /* This is the best we can do. */

  name = malloc (50);

  do
    {
      sprintf (name, ".nfstmpgnu.%d", n++);
      err = netfs_attempt_create_file (cred, dir, name, mode, newnp);
      if (err == EEXIST)
	mutex_lock (&dir->lock);
    }
  while (err == EEXIST);
  
  if (err)
    {
      free (name);
      return err;
    }

  assert (!(*newnp)->nn->dead_dir);
  assert (!(*newnp)->nn->dead_name);
  netfs_nref (dir);
  (*newnp)->nn->dead_dir = dir;
  (*newnp)->nn->dead_name = name;
  if ((*newnp)->nn->dtrans == NOT_POSSIBLE)
    (*newnp)->nn->dtrans = POSSIBLE;
  return 0;
}

/* Implement the netfs_attempt_create_file callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_create_file (struct netcred *cred, struct node *np,
			   char *name, mode_t mode, struct node **newnp)
{
  int *p;
  void *rpcbuf;
  error_t err;

  p = nfs_initialize_rpc (NFSPROC_CREATE, cred, 0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  p = xdr_encode_string (p, name);
  p = xdr_encode_create_state (p, mode);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));

  mutex_unlock (&np->lock);

  if (!err)
    {
      *newnp = lookup_fhandle (p);
      p += NFS_FHSIZE / sizeof (int);
      register_fresh_stat (*newnp, p);
    }
  else
    *newnp = 0;
  
  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_unlink callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_unlink (struct netcred *cred, struct node *dir,
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
      mutex_lock (&dir->lock);
      return err;
    }

  /* See if there are any other users of this node than the 
     one we just got; if so, we must give this file another link
     so that when we delete the one we are asked for it doesn't go
     away entirely. */
  if (np->references > 1)
    {
      char *newname;
      int n = 0;

      newname = malloc (50);
      mutex_unlock (&np->lock);
      do
	{
	  sprintf (newname, ".nfs%xgnu.%d", (int) np, n++);
	  err = netfs_attempt_link (cred, dir, np, newname);
	}
      while (err == EEXIST);

      if (err)
	{
	  free (newname);
	  mutex_lock (&dir->lock);
	  netfs_nrele (np);
	  return err;
	}

      /* Write down what name we gave it; we'll delete this when all
	 our uses vanish.  */
      mutex_lock (&np->lock);
      if (np->nn->dead_dir)
	netfs_nrele (np->nn->dead_dir);
      netfs_nref (dir);
      np->nn->dead_dir = dir;
      if (np->nn->dead_name)
	free (np->nn->dead_name);
      np->nn->dead_name = newname;
      if (np->nn->dtrans == NOT_POSSIBLE)
	np->nn->dtrans = POSSIBLE;
    }
  netfs_nput (np);

  mutex_lock (&dir->lock);
  p = nfs_initialize_rpc (NFSPROC_REMOVE, cred, 0, &rpcbuf, dir, -1);
  p = xdr_encode_fhandle (p, &dir->nn->handle);
  p = xdr_encode_string (p, name);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  free (rpcbuf);

  return err;
}

/* Implement the netfs_attempt_rename callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_rename (struct netcred *cred, struct node *fromdir,
		      char *fromname, struct node *todir, char *toname)
{
  int *p;
  void *rpcbuf;
  error_t err;
  
  mutex_lock (&fromdir->lock);
  p = nfs_initialize_rpc (NFSPROC_RENAME, cred, 0, &rpcbuf, fromdir, -1);
  p = xdr_encode_fhandle (p, &fromdir->nn->handle);
  p = xdr_encode_string (p, fromname);
  mutex_unlock (&fromdir->lock);
  
  mutex_lock (&todir->lock);
  p = xdr_encode_fhandle (p, &todir->nn->handle);
  p = xdr_encode_string (p, toname);
  mutex_unlock (&todir->lock);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  free (rpcbuf);
  return err;
}

/* Implement the netfs_attempt_readlink callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_readlink (struct netcred *cred, struct node *np,
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
  
  p = nfs_initialize_rpc (NFSPROC_READLINK, cred, 0, &rpcbuf, np, -1);
  p = xdr_encode_fhandle (p, &np->nn->handle);
  
  err = conduct_rpc (&rpcbuf, &p);
  if (!err)
    err = nfs_error_trans (ntohl (*p++));
  
  if (!err)
    p = xdr_decode_string (p, buf);

  free (rpcbuf);
  return err;
}

/* For an NFS node NODE, guess whether CRED is able to read or write
   it by hoping the server uses permissions bits in the "expected
   way".  Return the or of O_READ, O_WRITE, and O_EXEC accordingly as
   each is possible. */
static int
guess_mode_use (struct node *np, 
		struct netcred *cred)
{
  error_t err;

  err = netfs_validate_stat (np, cred);
  if (err)
    return err;
  
  if (cred_has_uid (cred, 0))
    return O_READ|O_WRITE|O_EXEC;
  else if (cred->nuids == 0
	   && (np->nn_stat.st_mode & S_IUSEUNK))
    return 
      (((np->nn_stat.st_mode & 04000000) ? O_READ : 0)
       | ((np->nn_stat.st_mode & 02000000) ? O_WRITE : 0)
       | ((np->nn_stat.st_mode & 01000000) ? O_EXEC : 0));
  else if (cred_has_uid (cred, np->nn_stat.st_uid)
	   || (cred_has_gid (cred, np->nn_stat.st_gid)
	       && cred_has_uid (cred, np->nn_stat.st_gid)))
    /* Owner */
    return 
      (((np->nn_stat.st_mode & 0400) ? O_READ : 0)
       | ((np->nn_stat.st_mode & 0200) ? O_WRITE : 0)
       | ((np->nn_stat.st_mode & 0100) ? O_EXEC : 0));
  else if (cred_has_gid (cred, np->nn_stat.st_gid))
    /* Group */
    return 
      (((np->nn_stat.st_mode & 040) ? O_READ : 0)
       | ((np->nn_stat.st_mode & 020) ? O_WRITE : 0)
       | ((np->nn_stat.st_mode & 010) ? O_EXEC : 0));
  else
    /* Other */
    return 
      (((np->nn_stat.st_mode & 4) ? O_READ : 0)
       | ((np->nn_stat.st_mode & 2) ? O_WRITE : 0)
       | ((np->nn_stat.st_mode & 1) ? O_EXEC : 0));
}

/* Implement the netfs_check_open_permissions callback as described in
   <hurd/netfs.h>. */
error_t
netfs_check_open_permissions (struct netcred *cred, struct node *np,
			      int flags, int newnode)
{
  if ((flags & (O_READ|O_WRITE|O_EXEC)) == 0)
    return 0;
  
  if ((flags & (O_READ|O_WRITE|O_EXEC))
      == (flags & guess_mode_use (np, cred)))
    return 0;
  else
    return EACCES;
}

/* Implement the netfs_report_access callback as described in
   <hurd/netfs.h>. */
void
netfs_report_access (struct netcred *cred,
		     struct node *np,
		     int *types)
{
  *types = guess_mode_use (np, cred);
}


/* These definitions have unfortunate side effects, don't use them,
   clever though they are. */
#if 0
/* Implement the netfs_check_open_permissions callback as described in
   <hurd/netfs.h>. */
error_t
netfs_check_open_permissions (struct netcred *cred, struct node *np,
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
	  *p++ = 0;
	  *p++ = htonl (50);
	  err = conduct_rpc (&rpcbuf, &p);
	  if (!err)
	    err = nfs_error_trans (ntohl (*p++));
	  free (rpcbuf);
	  
	  if (err)
	    return err;
	}
      return 0;
    }
}
      
/* Implement the netfs_report_access callback as described in
   <hurd/netfs.h>. */
void
netfs_report_access (struct netcred *cred,
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
  assert (len == 1 || len == 0);

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
   *BUFP and TOTALENTRIES to the total number of entries copied. */
static error_t
fetch_directory (struct netcred *cred, struct node *dir,
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
  bp = buf;
  cookie = 0;
  eof = 0;
  *totalentries = 0;

  while (!eof)
    {
      /* Fetch new directory entries */
      p = nfs_initialize_rpc (NFSPROC_READDIR, cred, 0, &rpcbuf, dir, -1);
      p = xdr_encode_fhandle (p, &dir->nn->handle);
      *p++ = cookie;
      *p++ = ntohl (read_size);
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	err = nfs_error_trans (ntohl (*p++));
      if (err)
	{
	  free (buf);
	  return err;
	}

      isnext = ntohl (*p++);
      
      /* Now copy them one at a time. */
      while (isnext)
	{
	  ino_t fileno;
	  int namlen;
	  int reclen;
	  
	  fileno = ntohl (*p++);
	  namlen = ntohl (*p++);

	  /* There's a hidden +1 here for the null byte and -1 because d_name
	     has a size of one already in the sizeof.  */
	  reclen = sizeof (struct dirent) + namlen;
	  reclen = (reclen + 3) & ~3; /* make it a multiple of four */
	  
	  /* Expand buffer if necessary */
	  if (bp + reclen > buf + bufmalloced)
	    {
	      char *newbuf;
	      
	      newbuf = realloc (buf, bufmalloced *= 2);
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
	  bcopy (p, entry->d_name, namlen);
	  entry->d_name[namlen] = '\0';

	  p += INTSIZE (namlen);
	  bp = bp + entry->d_reclen;

	  ++*totalentries;
	  
	  cookie = *p++;
	  isnext = ntohl (*p++);
	}

      eof = ntohl (*p++);
      free (rpcbuf);
    }

  /* Return it all to the user */
  *bufp = buf;
  *bufsizep = bufmalloced;
  return 0;
}

	 
/* Implement the netfs_get_directs callback as described in
   <hurd/netfs.h>. */
error_t
netfs_get_dirents (struct netcred *cred, struct node *np,
		   int entry, int nentries, char **data,
		   mach_msg_type_number_t *datacnt,
		   vm_size_t bufsiz, int *amt)
{
  void *buf;
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
    vm_allocate (mach_task_self (), (vm_address_t *)data, allocsize, 1);
 
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
	bcopy (bp, userdp, entry->d_reclen);
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
    vm_deallocate (mach_task_self (), round_page (userdp),
		   round_page (allocsize) - round_page (userdp - *data));

  *datacnt = userdp - *data;
  return 0;
}

  
/* Implement the netfs_set_translator callback as described in
   <hurd/netfs.h>. */
error_t
netfs_set_translator (struct netcred *cred,
		      struct node *np,
		      char *argz,
		      size_t argzlen)
{
  return EOPNOTSUPP;
}

/* Implement the netfs_attempt_mksymlink callback as described in
   <hurd/netfs.h>. */
error_t
netfs_attempt_mksymlink (struct netcred *cred,
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
   <hurd/netfs.h>. */
error_t
netfs_attempt_mkdev (struct netcred *cred,
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


