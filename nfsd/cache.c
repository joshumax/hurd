/* 
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


#include <string.h>
#include <hurd/fsys.h>
#include <assert.h>
#include <string.h>
#include "nfsd.h"


#undef TRUE
#undef FALSE
#define malloc spoogie_woogie	/* ugh^2. */
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/auth_unix.h>
#undef malloc

#define IDHASH_TABLE_SIZE 1024
#define FHHASH_TABLE_SIZE 1024
#define REPLYHASH_TABLE_SIZE 1024


static struct idspec *idhashtable[IDHASH_TABLE_SIZE];
spin_lock_t idhashlock = SPIN_LOCK_INITIALIZER;
static int nfreeids;
static int leastidlastuse;

/* Compare I against the specified set of users/groups. */
/* Use of int in decl of UIDS and GIDS is correct here; that's
   the NFS type because they come in in known 32 bit slots. */
static int
idspec_compare (struct idspec *i, int nuids, int ngids,
		int *uids, int *gids)
{
  if (i->nuids != nuids
      || i->ngids != ngids)
    return 0;

  assert (sizeof (int) == sizeof (uid_t));
  
  if (bcmp (i->uids, uids, nuids * sizeof (uid_t))
      || bcmp (i->gids, gids, ngids * sizeof (gid_t)))
    return 0;
 
  return 1;
}

/* Compute a hash value for a given user spec */
static int
idspec_hash (int nuids, int ngids, int *uids, int *gids)
{
  int hash, n;
  
  hash = nuids + ngids;
  for (n = 0; n < ngids; n++)
    hash += gids[n];
  for (n = 0; n < nuids; n++)
    hash += uids[n];
  hash %= IDHASH_TABLE_SIZE;
  return hash;
}

/* Lookup a user spec in the hash table and allocate a reference */
static struct idspec *
idspec_lookup (int nuids, int ngids, int *uids, int *gids)
{
  int hash;
  struct idspec *i;

  hash = idspec_hash (nuids, ngids, uids, gids);

  spin_lock (&idhashlock);
  for (i = idhashtable[hash]; i; i = i->next)
    if (idspec_compare (i, nuids, ngids, uids, gids))
      {
	i->references++;
	if (i->references == 1)
	  nfreeids--;
	spin_unlock (&idhashlock);
	return i;
      }
  
  assert (sizeof (uid_t) == sizeof (int));
  i = malloc (sizeof (struct idspec));
  i->nuids = nuids;
  i->ngids = ngids;
  i->uids = malloc (nuids * sizeof (uid_t));
  i->gids = malloc (ngids * sizeof (gid_t));
  bcopy (uids, i->uids, nuids * sizeof (uid_t));
  bcopy (gids, i->gids, ngids * sizeof (gid_t));
  i->references = 1;

  i->next = idhashtable[hash];
  if (idhashtable[hash])
    idhashtable[hash]->prevp = &i->next;
  i->prevp = &idhashtable[hash];
  idhashtable[hash] = i;

  spin_unlock (&idhashlock);
  return i;
}

int *
process_cred (int *p, struct idspec **credp)
{
  int type;
  int len;
  int *uid;
  int *gids;
  int ngids;
  int firstgid;
  int i;
  
  type = ntohl (*p++);

  if (type != AUTH_UNIX)
    {
      int size = ntohl (*p++);
      *credp = idspec_lookup (0, 0, 0, 0);
      return p + INTSIZE (size);
    }
  
  p++;				/* skip size */
  p++;				/* skip seconds */
  len = ntohl (*p++);
  p += INTSIZE (len);		/* skip hostname */
  
  uid = p++;			/* remember loc of uid */
  *uid = ntohl (*uid);
  
  firstgid = *p++;		/* remember first gid */
  gids = p;			/* here's where the array will start */
  ngids = ntohl (*p++);
  
  /* Now swap the first gid to be the first element of the array */
  *gids = firstgid;
  ngids++;			/* and count it */

  /* And byteswap the gids */
  for (i = 1; i < ngids; i++)
    gids[i] = ntohl (gids[i]);
  
  /* Next is the verf field; skip it entirely */
  p++;				/* skip id */
  len = htonl (*p++);
  p += INTSIZE (len);
  
  *credp = idspec_lookup (1, ngids, uid, gids);
  return p;
}

void
cred_rele (struct idspec *i)
{
  spin_lock (&idhashlock);
  i->references--;
  if (i->references == 0)
    {
      i->lastuse = mapped_time->seconds;
      if (i->lastuse < leastidlastuse || nfreeids == 0)
	leastidlastuse = i->lastuse;
      nfreeids++;
    }
  spin_unlock (&idhashlock);
}

void
cred_ref (struct idspec *i)
{
  spin_lock (&idhashlock);
  assert (i->references);
  i->references++;
  spin_unlock (&idhashlock);
}

void
scan_creds ()
{
  struct idspec *i;
  int n;
  int newleast = mapped_time->seconds;

  spin_lock (&idhashlock);
  if (mapped_time->seconds - leastidlastuse > ID_KEEP_TIMEOUT)
    for (n = 0; n < IDHASH_TABLE_SIZE && nfreeids; n++)
      for (i = idhashtable[n]; i && nfreeids; i = i->next)
	if (!i->references
	    && mapped_time->seconds - i->lastuse > ID_KEEP_TIMEOUT)
	  {
	    nfreeids--;
	    *i->prevp = i->next;
	    if (i->next)
	      i->next->prevp = i->prevp;
	    free (i->uids);
	    free (i->gids);
	    free (i);
	  }
	else
	  if (!i->references && newleast > i->lastuse)
	    newleast = i->lastuse;

  /* If we didn't bail early, then this is valid */
  if (nfreeids)
    leastidlastuse = newleast;
  spin_unlock (&idhashlock);
}



static struct cache_handle *fhhashtable[FHHASH_TABLE_SIZE];
struct mutex fhhashlock = MUTEX_INITIALIZER;
static int nfreefh;
static int leastfhlastuse;

static int
fh_hash (char *fhandle, struct idspec *i)
{
  int hash = 0, n;
  
  for (n = 0; n < NFS_FHSIZE; n++)
    hash += fhandle[n];
  hash += (int) i >> 6;
  return hash;
}

int *
lookup_cache_handle (int *p, struct cache_handle **cp, struct idspec *i)
{
  int hash;
  struct cache_handle *c;
  fsys_t fsys;
  file_t port;
  
  hash = fh_hash ((char *)p, i);
  mutex_lock (&fhhashlock);
  for (c = fhhashtable[hash]; c; c = c->next)
    if (c->ids == i && ! bcmp (c->handle, p, NFS_FHSIZE))
      {
	if (c->references == 0)
	  nfreefh--;
	c->references++;
	mutex_unlock (&fhhashlock);
	*cp = c;
	return p + NFS_FHSIZE / sizeof (int);
      }
  
  /* Not found */
  
  /* First four bytes are our internal table of filesystems */
  fsys = lookup_filesystem (*p);
  if (fsys == MACH_PORT_NULL
      || fsys_getfile (fsys, i->uids, i->nuids, i->gids, i->ngids,
		       (char *)(p + 1), NFS_FHSIZE - sizeof (int), &port))
    {
      mutex_unlock (&fhhashlock);
      *cp = 0;
      return p + NFS_FHSIZE / sizeof (int);
    }
  
  c = malloc (sizeof (struct cache_handle));
  bcopy (p, c->handle, NFS_FHSIZE);
  cred_ref (i);
  c->ids = i;
  c->port = port;
  c->references = 1;

  c->next = fhhashtable[hash];
  if (c->next)
    c->next->prevp = &c->next;
  c->prevp = &fhhashtable[hash];
  fhhashtable[hash] = c;
  
  mutex_unlock (&fhhashlock);
  *cp = c;
  return p + NFS_FHSIZE / sizeof (int);
}

void
cache_handle_rele (struct cache_handle *c)
{
  mutex_lock (&fhhashlock);
  c->references--;
  if (c->references == 0)
    {
      c->lastuse = mapped_time->seconds;
      if (c->lastuse < leastfhlastuse || nfreefh == 0)
	leastfhlastuse = c->lastuse;
      nfreefh++;
    }
  mutex_unlock (&fhhashlock);
}

void  
scan_fhs ()
{
  struct cache_handle *c;
  int n;
  int newleast = mapped_time->seconds;
  
  mutex_lock (&fhhashlock);
  if (mapped_time->seconds - leastfhlastuse > FH_KEEP_TIMEOUT)
    for (n = 0; n < FHHASH_TABLE_SIZE && nfreefh; n++)
      for (c = fhhashtable[n]; c && nfreefh; c = c->next)
	if (!c->references
	    && mapped_time->seconds - c->lastuse > FH_KEEP_TIMEOUT)
	  {
	    nfreefh--;
	    *c->prevp = c->next;
	    if (c->next)
	      c->next->prevp = c->prevp;
	    cred_rele (c->ids);
	    mach_port_deallocate (mach_task_self (), c->port);
	    free (c);
	  }
	else
	  if (!c->references && newleast > c->lastuse)
	    newleast = c->lastuse;
  
  /* If we didn't bail early, then this is valid. */
  if (nfreefh)
    leastfhlastuse = newleast;
  mutex_unlock (&fhhashlock);
}

struct cache_handle *
create_cached_handle (int fs, struct cache_handle *credc, file_t newport)
{
  char fhandle[NFS_FHSIZE];
  error_t err;
  struct cache_handle *c;
  int hash;
  char *bp = fhandle + sizeof (int);
  size_t handlelen = NFS_FHSIZE - sizeof (int);

  *(int *)fhandle = fs;
  err = file_getfh (newport, &bp, &handlelen);
  if (err || handlelen != NFS_FHSIZE - sizeof (int))
    {
      mach_port_deallocate (mach_task_self (), newport);
      return 0;
    }
  if (bp != fhandle + sizeof (int))
    {
      bcopy (bp, fhandle + sizeof (int), NFS_FHSIZE - sizeof (int));
      vm_deallocate (mach_task_self (), (vm_address_t) bp, handlelen);
    }
  
  hash = fh_hash (fhandle, credc->ids);
  mutex_lock (&fhhashlock);
  for (c = fhhashtable[hash]; c; c = c->next)
    if (c->ids == credc->ids && ! bcmp (fhandle, c->handle, NFS_FHSIZE))
      {
	/* Return this one */
	if (c->references == 0)
	  nfreefh--;
	c->references++;
	mutex_unlock (&fhhashlock);
	mach_port_deallocate (mach_task_self (), newport);
	return c;
      }
  
  /* Create it anew */
  c = malloc (sizeof (struct cache_handle));
  bcopy (fhandle, c->handle, NFS_FHSIZE);
  cred_ref (credc->ids);
  c->ids = credc->ids;
  c->port = newport;
  c->references = 1;
  
  /* And add it to the hash table */
  c->next = fhhashtable[hash];
  if (c->next)
    c->next->prevp = &c->next;
  c->prevp = &fhhashtable[hash];
  fhhashtable[hash] = c;
  mutex_unlock (&fhhashlock);
    
  return c;
}



static struct cached_reply *replyhashtable [REPLYHASH_TABLE_SIZE];
static spin_lock_t replycachelock = SPIN_LOCK_INITIALIZER;
static int nfreereplies;
static int leastreplylastuse;

/* Check the list of cached replies to see if this is a replay of a
   previous transaction; if so, return the cache record.  Otherwise,
   create a new cache record. */
struct cached_reply *
check_cached_replies (int xid, 
		      struct sockaddr_in *sender)
{
  struct cached_reply *cr;
  int hash;

  hash = xid % REPLYHASH_TABLE_SIZE;
  
  spin_lock (&replycachelock);
  for (cr = replyhashtable[hash]; cr; cr = cr->next)
    if (cr->xid == xid 
	&& !bcmp (sender, &cr->source, sizeof (struct sockaddr_in)))
      {
	cr->references++;
	if (cr->references == 1)
	  nfreereplies--;
	spin_unlock (&replycachelock);
	mutex_lock (&cr->lock);
	return cr;
      }

  cr = malloc (sizeof (struct cached_reply));
  mutex_init (&cr->lock);
  mutex_lock (&cr->lock);
  bcopy (sender, &cr->source, sizeof (struct sockaddr_in));
  cr->xid = xid;
  cr->data = 0;

  cr->next = replyhashtable[hash];
  if (replyhashtable[hash])
    replyhashtable[hash]->prevp = &cr->next;
  cr->prevp = &replyhashtable[hash];
  replyhashtable[hash] = cr;

  spin_unlock (&replycachelock);
  return cr;
}

/* A cached reply returned by check_cached_replies is now no longer
   needed by its caller. */
void
release_cached_reply (struct cached_reply *cr)
{
  mutex_unlock (&cr->lock);
  spin_lock (&replycachelock);
  cr->references--;
  if (cr->references == 0)
    {
      cr->lastuse = mapped_time->seconds;
      if (cr->lastuse < leastreplylastuse || nfreereplies == 0)
	leastreplylastuse = cr->lastuse;
      nfreereplies++;
    }
  spin_unlock (&replycachelock);
}

void
scan_replies ()
{
  struct cached_reply *cr;
  int n;
  int newleast = mapped_time->seconds;
  
  spin_lock (&replycachelock);
  if (mapped_time->seconds - leastreplylastuse > REPLY_KEEP_TIMEOUT)
    for (n = 0; n < REPLYHASH_TABLE_SIZE && nfreereplies; n++)
      for (cr = replyhashtable[n]; cr && nfreereplies; cr = cr->next)
	if (!cr->references
	    && mapped_time->seconds - cr->lastuse > REPLY_KEEP_TIMEOUT)
	  {
	    nfreereplies--;
	    *cr->prevp = cr->next;
	    if (cr->next)
	      cr->next->prevp = cr->prevp;
	    if (cr->data)
	      free (cr->data);
	  }
	else 
	  if (!cr->references && newleast > cr->lastuse)
	    newleast = cr->lastuse;
  
  /* If we didn't bail early, then this is valid */
  if (nfreereplies)
    leastreplylastuse = newleast;
  spin_unlock (&replycachelock);
}
