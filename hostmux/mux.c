/* Root hostmux node

   Copyright (C) 1997 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>
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

#include <stddef.h>
#include <string.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "hostmux.h"

error_t create_host_node (struct hostmux *mux, struct hostmux_name *name,
			  struct node **node);

/* Returned directory entries are aligned to blocks this many bytes long.
   Must be a power of two.  */
#define DIRENT_ALIGN 4
#define DIRENT_NAME_OFFS offsetof (struct dirent, d_name)

/* Length is structure before the name + the name + '\0', all
   padded to a four-byte alignment.  */
#define DIRENT_LEN(name_len)						      \
  ((DIRENT_NAME_OFFS + (name_len) + 1 + (DIRENT_ALIGN - 1))		      \
   & ~(DIRENT_ALIGN - 1))

static error_t lookup_host (struct hostmux *mux, const char *host,
			    struct node **node); /* fwd decl */

/* [root] Directory operations.  */

/* Lookup NAME in DIR for USER; set *NODE to the found name upon return.  If
   the name was not found, then return ENOENT.  On any error, clear *NODE.
   (*NODE, if found, should be locked, this call should unlock DIR no matter
   what.) */
error_t
netfs_attempt_lookup (struct iouser *user, struct node *dir, 
		      char *name, struct node **node)
{
  error_t err;

  if (dir->nn->name)
    err = ENOTDIR;
  else
    err = lookup_host (dir->nn->mux, name, node);

  fshelp_touch (&dir->nn_stat, TOUCH_ATIME, hostmux_maptime);

  mutex_unlock (&dir->lock);

  if (! err)
    mutex_lock (&(*node)->lock);

  return err;
}

/* Implement the netfs_get_directs callback as described in
   <hurd/netfs.h>. */
error_t
netfs_get_dirents (struct iouser *cred, struct node *dir,
		   int first_entry, int num_entries, char **data,
		   mach_msg_type_number_t *data_len,
		   vm_size_t max_data_len, int *data_entries)
{
  error_t err;
  int count;
  size_t size = 0;		/* Total size of our return block.  */
  struct hostmux_name *first_name, *nm;

  if (dir->nn->name)
    return ENOTDIR;

  rwlock_reader_lock (&dir->nn->mux->names_lock);

  /* Find the first entry.  */
  for (first_name = dir->nn->mux->names;
       first_name && first_entry > 0;
       first_name = first_name->next)
    if (first_name->node)
      first_entry--;

  /* See how much space we need for the result.  */
  for (nm = first_name, count = 0;
       nm && (num_entries == -1 || count < num_entries);
       nm = nm->next)
    if (nm->node)
      {
	size_t new_size = size + DIRENT_LEN (strlen (nm->name));
	if (max_data_len > 0 && new_size > max_data_len)
	  break;
	size = new_size;
	count++;
      }

  /* Allocate it.  */
  err = vm_allocate (mach_task_self (), (vm_address_t *) data, size, 1);
  if (! err)
    /* Copy out the result.  */
    {
      char *p = *data;

      *data_len = size;
      *data_entries = count;

      /* See how much space we need for the result.  */
      for (nm = first_name, count = 0;
	   nm && (num_entries == -1 || count < num_entries);
	   nm = nm->next)
	if (nm->node)
	  {
	    struct dirent hdr;
	    size_t name_len = strlen (nm->name);
	    size_t sz = DIRENT_LEN (name_len);

	    if (sz > size)
	      break;
	    else
	      size -= sz;

	    hdr.d_fileno = nm->fileno;
	    hdr.d_reclen = sz;
	    hdr.d_type =
	      (strcmp (nm->canon, nm->name) == 0 ? DT_REG : DT_LNK);
	    hdr.d_namlen = name_len;

	    memcpy (p, &hdr, DIRENT_NAME_OFFS);
	    strcpy (p + DIRENT_NAME_OFFS, nm->name);
	    p += sz;

	    count++;
	  }

    }

  rwlock_reader_unlock (&dir->nn->mux->names_lock);

  fshelp_touch (&dir->nn_stat, TOUCH_ATIME, hostmux_maptime);

  return err;
}

/* Host lookup.  */

/* Free storage allocated consumed by the host mux name NM, but not the node
   it points to.  */
static void
free_name (struct hostmux_name *nm)
{
  free ((char *)nm->name);
  if (nm->name != nm->canon)
    free ((char *)nm->canon);
  free (nm);
}

/* See if there's an existing entry for the name HOST, and if so, return its
   node in NODE with an additional references.  True is returned iff the
   lookup succeeds.  If PURGE is true, then any nodes with a null node are
   removed.  */
static int
lookup_cached (struct hostmux *mux, const char *host, int purge,
	       struct node **node)
{
  struct hostmux_name *nm = mux->names, **prevl = &mux->names;

  while (nm)
    {
      struct hostmux_name *next = nm->next;

      if (strcasecmp (host, nm->name) == 0)
	{
	  spin_lock (&netfs_node_refcnt_lock);
	  if (nm->node)
	    nm->node->references++;
	  spin_unlock (&netfs_node_refcnt_lock);

	  if (nm->node)
	    {
	      *node = nm->node;
	      return 1;
	    }
	}

      if (purge && !nm->node)
	{
	  *prevl = nm->next;
	  free_name (nm);
	}
      else
	prevl = &nm->next;

      nm = next;
    }

  return 0;
}

/* See if there's an existing entry for the name HOST, and if so, return its
   node in NODE, with an additional reference, otherwise, create a new node
   for the host HE as referred to by HOST, and return that instead, with a
   single reference.  The type of node created is either a translator node,
   if HOST refers to the official name of the host, or a symlink node to the
   official name, if it doesn't.  */
static error_t
lookup_hostent (struct hostmux *mux, const char *host, struct hostent *he,
		struct node **node)
{
  error_t err;
  struct hostmux_name *nm = malloc (sizeof (struct hostmux_name));

  if (! nm)
    return ENOMEM;

  nm->name = strdup (host);
  if (strcmp (host, he->h_name) == 0)
    nm->canon = nm->name;
  else
    nm->canon = strdup (he->h_name);

  err = create_host_node (mux, nm, node);
  if (err)
    {
      free_name (nm);
      return err;
    }

  rwlock_writer_lock (&mux->names_lock);
  if (lookup_cached (mux, host, 1, node))
    /* An entry for HOST has already been created between the time we last
       looked and now (which is possible because we didn't lock MUX).
       Just throw away our version and return the one already in the cache.  */
    {
      rwlock_writer_unlock (&mux->names_lock);
      nm->node->nn->name = 0;	/* Avoid touching the mux name list.  */
      netfs_nrele (nm->node);	/* Free the tentative new node.  */
      free_name (nm);		/* And the name it was under.  */
    }
  else
    /* Enter NM into MUX's list of names, and return the new node.  */
    {
      nm->fileno = mux->next_fileno++; /* Now that we hold the lock...  */
      nm->next = mux->names;
      mux->names = nm;
      rwlock_writer_unlock (&mux->names_lock);
    }

  return 0;
}  

/* Lookup the host HOST in MUX, and return the resulting node in NODE, with
   an additional reference, or an error.  */
static error_t
lookup_host (struct hostmux *mux, const char *host, struct node **node)
{
  int was_cached;
  int h_err;
  struct hostent _he, *he;
  struct in_addr inet_addr;
  char hostent_data[2048];	/* XXX what size should this be???? */

  rwlock_reader_lock (&mux->names_lock);
  was_cached = lookup_cached (mux, host, 0, node);
  rwlock_reader_unlock (&mux->names_lock);

  if (was_cached)
    return 0;
  else if (inet_aton (host, &inet_addr))
    {
      if (gethostbyaddr_r ((char *)&inet_addr, sizeof inet_addr, AF_INET,
			   &_he, hostent_data, sizeof hostent_data,
			   &he, &h_err) == 0)
	return lookup_hostent (mux, host, he, node);
      else
	return ENOENT;
    }
  else if (gethostbyname_r (host, &_he, hostent_data, sizeof hostent_data,
			    &he, &h_err) == 0)
    return lookup_hostent (mux, host, he, node);
  else
    return ENOENT;
}

/* This should sync the entire remote filesystem.  If WAIT is set, return
   only after sync is completely finished.  */
error_t
netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  return 0;
}

/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the owner to UID and the group to GID. */
error_t
netfs_attempt_chown (struct iouser *cred, struct node *node, uid_t uid, uid_t gid)
{
  if (node->nn->name)
    return EOPNOTSUPP;
  else
    {
      struct hostmux *mux = node->nn->mux;
      error_t err = file_chown (mux->underlying, uid, gid);

      if (! err)
	{
	  struct hostmux_name *nm;

	  /* Change NODE's owner.  */
	  mux->stat_template.st_uid = uid;
	  mux->stat_template.st_gid = gid;
	  node->nn_stat.st_uid = uid;
	  node->nn_stat.st_gid = gid;

	  /* Change the owner of each leaf node.  */
	  rwlock_reader_lock (&mux->names_lock);
	  for (nm = mux->names; nm; nm = nm->next)
	    if (nm->node)
	      {
		nm->node->nn_stat.st_uid = uid;
		nm->node->nn_stat.st_gid = gid;
	      }
	  rwlock_reader_unlock (&mux->names_lock);

	  fshelp_touch (&node->nn_stat, TOUCH_CTIME, hostmux_maptime);
	}

      return err;
    }
}

/* This should attempt a chauthor call for the user specified by CRED on node
   NODE, to change the author to AUTHOR. */
error_t
netfs_attempt_chauthor (struct iouser *cred, struct node *node, uid_t author)
{
  if (node->nn->name)
    return EOPNOTSUPP;
  else
    {
      struct hostmux *mux = node->nn->mux;
      error_t err = file_chauthor (mux->underlying, author);

      if (! err)
	{
	  struct hostmux_name *nm;

	  /* Change NODE's owner.  */
	  mux->stat_template.st_author = author;
	  node->nn_stat.st_author = author;

	  /* Change the owner of each leaf node.  */
	  rwlock_reader_lock (&mux->names_lock);
	  for (nm = mux->names; nm; nm = nm->next)
	    if (nm->node)
	      nm->node->nn_stat.st_author = author;
	  rwlock_reader_unlock (&mux->names_lock);

	  fshelp_touch (&node->nn_stat, TOUCH_CTIME, hostmux_maptime);
	}

      return err;
    }
}

/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the mode to MODE.  Unlike the normal Unix and Hurd meaning
   of chmod, this function is also used to attempt to change files into other
   types.  If such a transition is attempted which is impossible, then return
   EOPNOTSUPP.  */
error_t
netfs_attempt_chmod (struct iouser *cred, struct node *node, mode_t mode)
{
  mode &= ~S_ITRANS;
  if (node->nn->name || ((mode & S_IFMT) != (node->nn_stat.st_mode & S_IFMT)))
    return EOPNOTSUPP;
  else
    {
      error_t err = file_chmod (node->nn->mux->underlying, mode & ~S_IFMT);
      if (! err)
	{
	  node->nn_stat.st_mode = mode;
	  fshelp_touch (&node->nn_stat, TOUCH_CTIME, hostmux_maptime);
	}
      return err;
    }
}
