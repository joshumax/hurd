/*
   Copyright (C) 2008, 2009 Free Software Foundation, Inc.
   Written by Zheng Da.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <fcntl.h>
#include <dirent.h>
#include <stddef.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <ctype.h>

#include <hurd/netfs.h>

#include "netfs_impl.h"
#include "vdev.h"
#include "util.h"

#define DIRENTS_CHUNK_SIZE      (8*1024)
/* Returned directory entries are aligned to blocks this many bytes long.
 * Must be a power of two.  */
#define DIRENT_ALIGN 4
#define DIRENT_NAME_OFFS offsetof (struct dirent, d_name)

/* Length is structure before the name + the name + '\0', all
 *    padded to a four-byte alignment.  */
#define DIRENT_LEN(name_len)                                                  \
  ((DIRENT_NAME_OFFS + (name_len) + 1 + (DIRENT_ALIGN - 1))                   \
   & ~(DIRENT_ALIGN - 1))

extern struct stat underlying_node_stat;

int
is_num (char *str)
{
  for (; *str; str++)
    {
      if (!isdigit (*str))
	return 0;
    }
  return 1;
}

/* Make a new virtual node.  Always consumes the ports.  */
error_t
new_node (struct lnode *ln, struct node **np)
{
  error_t err = 0;
  struct netnode *nn = calloc (1, sizeof *nn);
  struct node *node;

  if (nn == 0)
    return ENOMEM;
  node = netfs_make_node (nn);
  if (node == 0)
    {
      free (nn);
      *np = NULL;
      return ENOMEM;
    }
  if (ln)
    ln->n = node;
  nn->ln = ln;
  *np = node;
  return err;
}

struct node *
lookup (char *name)
{
  struct lnode *ln = (struct lnode *) lookup_dev_by_name (name);

  char *copied_name = malloc (strlen (name) + 1);
  strcpy (copied_name, name);
  if (ln)
    {
      new_node (ln, &ln->n);
      ln->n->nn->name = copied_name;
      return ln->n;
    }
  else
    {
      struct node *n;
      new_node (ln, &n);
      n->nn->name = copied_name;
      return n;
    }
}

/* Attempt to create a file named NAME in DIR for USER with MODE.  Set *NODE
   to the new node upon return.  On any error, clear *NODE.  *NODE should be
   locked on success; no matter what, unlock DIR before returning.  */
error_t
netfs_attempt_create_file (struct iouser *user, struct node *dir,
			   char *name, mode_t mode, struct node **node)
{
  debug("");
  *node = 0;
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}

/* Node NODE is being opened by USER, with FLAGS.  NEWNODE is nonzero if we
   just created this node.  Return an error if we should not permit the open
   to complete because of a permission restriction. */
error_t
netfs_check_open_permissions (struct iouser *user, struct node *node,
			      int flags, int newnode)
{
  error_t err = 0;

  /*Cheks user's permissions*/
  if(flags & O_READ)
    err = fshelp_access(&node->nn_stat, S_IREAD, user);
  if(!err && (flags & O_WRITE))
    err = fshelp_access(&node->nn_stat, S_IWRITE, user);
  if(!err && (flags & O_EXEC))
    err = fshelp_access(&node->nn_stat, S_IEXEC, user);

  debug("the mode of node: %o, return result: %d",
	(node->nn_stat.st_mode & ~S_IFMT), err);
  /*Return the result of the check*/
  return err;
}

/* This should attempt a utimes call for the user specified by CRED on node
   NODE, to change the atime to ATIME and the mtime to MTIME. */
error_t
netfs_attempt_utimes (struct iouser *cred, struct node *node,
		      struct timespec *atime, struct timespec *mtime)
{
  debug("");
  return EOPNOTSUPP;
}

/* Return the valid access types (bitwise OR of O_READ, O_WRITE, and O_EXEC)
   in *TYPES for file NODE and user CRED.  */
error_t
netfs_report_access (struct iouser *cred, struct node *node, int *types)
{
  debug("");
  *types = 0;
  return 0;
}

/* Make sure that NP->nn_stat is filled with current information.  CRED
   identifies the user responsible for the operation.  */
error_t
netfs_validate_stat (struct node *node, struct iouser *cred)
{
  struct stat st;

  if (node->nn->ln)
    st = node->nn->ln->st;
  else
    st = underlying_node_stat;

  debug("node: %p", node);
  node->nn_translated = S_ISLNK (st.st_mode) ? S_IFLNK : 0;
  node->nn_stat = st;
  return 0;
}

/* This should sync the file NODE completely to disk, for the user CRED.  If
   WAIT is set, return only after sync is completely finished.  */
error_t
netfs_attempt_sync (struct iouser *cred, struct node *node, int wait)
{
  debug("");
  return 0;
}

error_t
netfs_get_dirents (struct iouser *cred, struct node *dir,
		   int first_entry, int max_entries, char **data,
		   mach_msg_type_number_t *data_len,
		   vm_size_t max_data_len, int *data_entries)
{
  error_t err;
  int count = 0;
  char *data_p;
  size_t size = (max_data_len == 0 || max_data_len > DIRENTS_CHUNK_SIZE
     ? DIRENTS_CHUNK_SIZE : max_data_len);
  debug ("");
  int
    add_dirent (const char * name, ino_t ino, int type)
      {
	/*If the required number of dirents has not been listed yet*/
	if((max_entries == -1) || (count < max_entries))
	  {
	    struct dirent hdr;
	    size_t name_len = strlen(name);
	    size_t sz = DIRENT_LEN(name_len);

	    /*If there is no room for this dirent*/
	    if ((data_p - *data) + sz > size)
	      {
		if (max_data_len > 0)
		  return 1;
		else
		  /* Try to grow our return buffer.  */
		  {
		    error_t err;
		    vm_address_t extension = (vm_address_t)(*data + size);
		    err = vm_allocate (mach_task_self (), &extension,
				       DIRENTS_CHUNK_SIZE, 0);
		    if (err)
		      {
			munmap (*data, size);
			return 1;
		      }
		    size += DIRENTS_CHUNK_SIZE;
		  }
	      }

	    /*setup the dirent*/
	    hdr.d_ino = ino;
	    hdr.d_reclen = sz;
	    hdr.d_type = type;
	    hdr.d_namlen = name_len;
	    memcpy(data_p, &hdr, DIRENT_NAME_OFFS);
	    strcpy(data_p + DIRENT_NAME_OFFS, name);
	    data_p += sz;

	    /*count the new dirent*/
	    ++count;
	  }
	return 0;
      }
  int add_each_dev (struct vether_device *dev)
    {
      struct lnode *ln = (struct lnode *) dev;
      add_dirent (ln->vdev.name, ln->st.st_ino, DT_CHR);
      return 0;
    }
  if (dir != netfs_root_node)
    return ENOTDIR;

  *data = mmap (0, size, PROT_READ | PROT_WRITE, MAP_ANON, 0, 0);
  err = ((void *) *data == (void *) -1) ? errno : 0;
  if (!err)
    {
      data_p = *data;
      if (first_entry < 2 + get_dev_num ())
	{
	  add_dirent (".", 2, DT_DIR);
	  add_dirent ("..", 2, DT_DIR);
	  foreach_dev_do (add_each_dev);
	}

      vm_address_t alloc_end = (vm_address_t)(*data + size);
      vm_address_t real_end = round_page (data_p);
      if (alloc_end > real_end)
	munmap ((caddr_t) real_end, alloc_end - real_end);
      *data_entries = count;
      debug ("first_entry is %d, count is %d", first_entry, count);
      *data_len = data_p - *data;
    }
  return err;
}

/* Lookup NAME in DIR for USER; set *NODE to the found name upon return.  If
   the name was not found, then return ENOENT.  On any error, clear *NODE.
   (*NODE, if found, should be locked, this call should unlock DIR no matter
   what.) */
error_t netfs_attempt_lookup (struct iouser *user, struct node *dir,
			      char *name, struct node **node)
{
  error_t err = 0;

  debug ("dir: %p, file name: %s", dir, name);

  if (strcmp(name, ".") == 0)
    {
      netfs_nref(dir);
      *node = dir;
      return 0;
    }
  else if (strcmp(name, "..") == 0)
    {
      /*The supplied node is always root*/
      err = ENOENT;
      *node = NULL;

      /*unlock the directory*/
      pthread_mutex_unlock (&dir->lock);

      /*stop here*/
      return err;
    }

  *node = lookup (name);
  pthread_mutex_lock (&(*node)->lock);
  pthread_mutex_unlock (&dir->lock);
  return 0;
}

/* Delete NAME in DIR for USER. */
error_t netfs_attempt_unlink (struct iouser *user, struct node *dir,
			      char *name)
{
  debug("");
  return EOPNOTSUPP;
}

/* Note that in this one call, neither of the specific nodes are locked. */
error_t netfs_attempt_rename (struct iouser *user, struct node *fromdir,
			      char *fromname, struct node *todir,
			      char *toname, int excl)
{
  debug("");
  return EOPNOTSUPP;
}

/* Attempt to create a new directory named NAME in DIR for USER with mode
   MODE.  */
error_t netfs_attempt_mkdir (struct iouser *user, struct node *dir,
			     char *name, mode_t mode)
{
  debug("");
  return EOPNOTSUPP;
}

/* Attempt to remove directory named NAME in DIR for USER. */
error_t netfs_attempt_rmdir (struct iouser *user,
			     struct node *dir, char *name)
{
  debug("");
  return EOPNOTSUPP;
}

/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the owner to UID and the group to GID. */
error_t netfs_attempt_chown (struct iouser *cred, struct node *node,
			     uid_t uid, uid_t gid)
{
  debug("");
  return EOPNOTSUPP;
}

/* This should attempt a chauthor call for the user specified by CRED on node
   NODE, to change the author to AUTHOR. */
error_t netfs_attempt_chauthor (struct iouser *cred, struct node *node,
				uid_t author)
{
  debug("");
  return EOPNOTSUPP;
}

/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the mode to MODE.  Unlike the normal Unix and Hurd meaning
   of chmod, this function is also used to attempt to change files into other
   types.  If such a transition is attempted which is impossible, then return
   EOPNOTSUPP.  */
error_t netfs_attempt_chmod (struct iouser *cred, struct node *node,
			     mode_t mode)
{
  error_t err = 0;
  debug("");
  if (node->nn->ln == NULL)
    return EOPNOTSUPP;

  mode &= ~S_ITRANS;
  err = fshelp_isowner (&node->nn->ln->st, cred);
  if (err)
    return err;
  mode |= node->nn->ln->st.st_mode & S_IFMT;
  node->nn->ln->st.st_mode = mode;
  fshelp_touch (&node->nn_stat, TOUCH_CTIME, multiplexer_maptime);
  return err;
}

/* Attempt to turn NODE (user CRED) into a symlink with target NAME. */
error_t netfs_attempt_mksymlink (struct iouser *cred, struct node *node,
				 char *name)
{
  debug("");
  return EOPNOTSUPP;
}

/* Attempt to turn NODE (user CRED) into a device.  TYPE is either S_IFBLK or
   S_IFCHR. */
error_t netfs_attempt_mkdev (struct iouser *cred, struct node *node,
			     mode_t type, dev_t indexes)
{
  debug("");
  return EOPNOTSUPP;
}

/* Attempt to set the passive translator record for FILE to ARGZ (of length
   ARGZLEN) for user CRED. */
error_t netfs_set_translator (struct iouser *cred, struct node *node,
			      char *argz, size_t argzlen)
{
  debug("");
  return EOPNOTSUPP;
}

/* This should attempt a chflags call for the user specified by CRED on node
   NODE, to change the flags to FLAGS. */
error_t netfs_attempt_chflags (struct iouser *cred, struct node *node,
			       int flags)
{
  debug("");
  return EOPNOTSUPP;
}

/* This should attempt to set the size of the file NODE (for user CRED) to
   SIZE bytes long. */
error_t netfs_attempt_set_size (struct iouser *cred, struct node *node,
				off_t size)
{
  debug("");
  return EOPNOTSUPP;
}

/*Fetches the filesystem status information*/
error_t
netfs_attempt_statfs (struct iouser *cred, struct node *node,
		      struct statfs *st)
{
  debug("");
  return EOPNOTSUPP;
}

/* This should sync the entire remote filesystem.  If WAIT is set, return
   only after sync is completely finished.  */
error_t netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  debug("");
  return 0;
}

/* Create a link in DIR with name NAME to FILE for USER.  Note that neither
   DIR nor FILE are locked.  If EXCL is set, do not delete the target, but
   return EEXIST if NAME is already found in DIR.  */
error_t netfs_attempt_link (struct iouser *user, struct node *dir,
			    struct node *file, char *name, int excl)
{
  debug("");
  return EOPNOTSUPP;
}

/* Attempt to create an anonymous file related to DIR for USER with MODE.
   Set *NODE to the returned file upon success.  No matter what, unlock DIR. */
error_t netfs_attempt_mkfile (struct iouser *user, struct node *dir,
			      mode_t mode, struct node **node)
{
  debug("");
  *node = 0;
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}

/* Read the contents of NODE (a symlink), for USER, into BUF. */
error_t netfs_attempt_readlink (struct iouser *user, struct node *node, char *buf)
{
  debug("");
  return EOPNOTSUPP;
}

/* Read from the file NODE for user CRED starting at OFFSET and continuing for
   up to *LEN bytes.  Put the data at DATA.  Set *LEN to the amount
   successfully read upon return.  */
error_t netfs_attempt_read (struct iouser *cred, struct node *node,
			    off_t offset, size_t *len, void *data)
{
  debug("");
  return EOPNOTSUPP;
}

/* Write to the file NODE for user CRED starting at OFSET and continuing for up
   to *LEN bytes from DATA.  Set *LEN to the amount seccessfully written upon
   return. */
error_t netfs_attempt_write (struct iouser *cred, struct node *node,
			     off_t offset, size_t *len, void *data)
{
  debug("");
  return EOPNOTSUPP;
}

/* Node NP is all done; free all its associated storage. */
void
netfs_node_norefs (struct node *node)
{
  debug("node: %p", node);
  if (node->nn->ln)
    node->nn->ln->n = NULL;
  free (node->nn->name);
  free (node->nn);
  free (node);
}

