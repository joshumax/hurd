/*
   Copyright (C) 2017 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Libnetfs callbacks */

#include "netfs_impl.h"

#include <stddef.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <hurd/netfs.h>

#include "pcifs.h"
#include "ncache.h"
#include <pciaccess.h>
#include "func_files.h"

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

/* Fetch a directory, as for netfs_get_dirents.  */
static error_t
get_dirents (struct pcifs_dirent *dir,
	     int first_entry, int max_entries, char **data,
	     mach_msg_type_number_t * data_len,
	     vm_size_t max_data_len, int *data_entries)
{
  struct pcifs_dirent *e;
  error_t err = 0;
  int i, count;
  size_t size;
  char *p;

  if (first_entry >= dir->dir->num_entries)
    {
      *data_len = 0;
      *data_entries = 0;
      return 0;
    }

  if (max_entries < 0)
    count = dir->dir->num_entries;
  else
    {
      count = ((first_entry + max_entries) >= dir->dir->num_entries ?
	       dir->dir->num_entries : max_entries) - first_entry;
    }

  size =
    (count * DIRENTS_CHUNK_SIZE) >
    max_data_len ? max_data_len : count * DIRENTS_CHUNK_SIZE;

  *data = mmap (0, size, PROT_READ | PROT_WRITE, MAP_ANON, 0, 0);
  err = ((void *) *data == (void *) -1) ? errno : 0;
  if (err)
    return err;

  p = *data;
  for (i = 0; i < count; i++)
    {
      struct dirent hdr;
      size_t name_len;
      size_t sz;
      int entry_type;

      e = dir->dir->entries[i + first_entry];
      name_len = strlen (e->name) + 1;
      sz = DIRENT_LEN (name_len);
      entry_type = IFTODT (e->stat.st_mode);

      hdr.d_namlen = name_len;
      hdr.d_fileno = e->stat.st_ino;
      hdr.d_reclen = sz;
      hdr.d_type = entry_type;

      memcpy (p, &hdr, DIRENT_NAME_OFFS);
      strncpy (p + DIRENT_NAME_OFFS, e->name, name_len);
      p += sz;
    }

  vm_address_t alloc_end = (vm_address_t) (*data + size);
  vm_address_t real_end = round_page (p);
  if (alloc_end > real_end)
    munmap ((caddr_t) real_end, alloc_end - real_end);
  *data_len = p - *data;
  *data_entries = count;

  return err;
}

static struct pcifs_dirent *
lookup (struct node *np, char *name)
{
  int i;
  struct pcifs_dirent *ret = 0, *e;

  for (i = 0; i < np->nn->ln->dir->num_entries; i++)
    {
      e = np->nn->ln->dir->entries[i];

      if (!strncmp (e->name, name, NAME_SIZE))
	{
	  ret = e;
	  break;
	}
    }

  return ret;
}

static error_t
create_node (struct pcifs_dirent * e, struct node ** node)
{
  struct node *np;
  struct netnode *nn;

  np = netfs_make_node_alloc (sizeof (struct netnode));
  if (!np)
    return ENOMEM;
  np->nn_stat = e->stat;
  np->nn_translated = np->nn_stat.st_mode;

  nn = netfs_node_netnode (np);
  memset (nn, 0, sizeof (struct netnode));
  nn->ln = e;

  *node = e->node = np;

  return 0;
}

static void
destroy_node (struct node *node)
{
  if (node->nn->ln)
    node->nn->ln->node = 0;
  free (node);
}

/* Attempt to create a file named NAME in DIR for USER with MODE.  Set *NODE
   to the new node upon return.  On any error, clear *NODE.  *NODE should be
   locked on success; no matter what, unlock DIR before returning.  */
error_t
netfs_attempt_create_file (struct iouser * user, struct node * dir,
			   char *name, mode_t mode, struct node ** node)
{
  *node = 0;
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}

/* Node NODE is being opened by USER, with FLAGS.  NEWNODE is nonzero if we
   just created this node.  Return an error if we should not permit the open
   to complete because of a permission restriction. */
error_t
netfs_check_open_permissions (struct iouser * user, struct node * node,
			      int flags, int newnode)
{
  return entry_check_perms (user, node->nn->ln, flags);
}

/* This should attempt a utimes call for the user specified by CRED on node
   NODE, to change the atime to ATIME and the mtime to MTIME. */
error_t
netfs_attempt_utimes (struct iouser * cred, struct node * node,
		      struct timespec * atime, struct timespec * mtime)
{
  return EOPNOTSUPP;
}

/* Return the valid access types (bitwise OR of O_READ, O_WRITE, and O_EXEC)
   in *TYPES for file NODE and user CRED.  */
error_t
netfs_report_access (struct iouser * cred, struct node * node, int *types)
{
  return EOPNOTSUPP;
}

/* Trivial definitions.  */

/* Make sure that NP->nn_stat is filled with current information.  CRED
   identifies the user responsible for the operation.  */
error_t
netfs_validate_stat (struct node * node, struct iouser * cred)
{
  /* Nothing to do here */
  return 0;
}

/* This should sync the file NODE completely to disk, for the user CRED.  If
   WAIT is set, return only after sync is completely finished.  */
error_t
netfs_attempt_sync (struct iouser * cred, struct node * node, int wait)
{
  return EOPNOTSUPP;
}

error_t
netfs_get_dirents (struct iouser * cred, struct node * dir,
		   int first_entry, int max_entries, char **data,
		   mach_msg_type_number_t * data_len,
		   vm_size_t max_data_len, int *data_entries)
{
  error_t err = 0;

  if (dir->nn->ln->dir)
    {
      err = get_dirents (dir->nn->ln, first_entry, max_entries,
			 data, data_len, max_entries, data_entries);
    }
  else
    err = ENOTDIR;

  if (!err)
    /* Update atime */
    UPDATE_TIMES (dir->nn->ln, TOUCH_ATIME);

  return err;
}

/* Lookup NAME in DIR for USER; set *NODE to the found name upon return.  If
   the name was not found, then return ENOENT.  On any error, clear *NODE.
   (*NODE, if found, should be locked, this call should unlock DIR no matter
   what.) */
error_t
netfs_attempt_lookup (struct iouser * user, struct node * dir,
		      char *name, struct node ** node)
{
  error_t err = 0;
  struct pcifs_dirent *entry;

  if (*name == '\0' || strcmp (name, ".") == 0)
    /* Current directory -- just add an additional reference to DIR's node
       and return it.  */
    {
      netfs_nref (dir);
      *node = dir;
      return 0;
    }
  else if (strcmp (name, "..") == 0)
    /* Parent directory.  */
    {
      if (dir->nn->ln->parent)
	{
	  *node = dir->nn->ln->parent->node;
	  pthread_mutex_lock (&(*node)->lock);
	  netfs_nref (*node);
	}
      else
	{
	  err = ENOENT;		/* No .. */
	  *node = 0;
	}

      pthread_mutex_unlock (&dir->lock);

      return err;
    }

  /* `name' is not . nor .. */
  if (dir->nn->ln->dir)
    {
      /* `dir' is a directory */

      /* Check dir permissions */
      err = entry_check_perms (user, dir->nn->ln, O_READ | O_EXEC);
      if (!err)
	{
	  entry = lookup (dir, name);
	  if (!entry)
	    {
	      err = ENOENT;
	    }
	  else
	    {
	      if (entry->node)
		{
		  netfs_nref (entry->node);
		}
	      else
		{
		  /*
		   * No active node, create one.
		   * The new node is created with a reference.
		   */
		  err = create_node (entry, node);
		}

	      if (!err)
		{
		  *node = entry->node;
		  /* We have to unlock DIR's node before locking the child node
		     because the locking order is always child-parent.  We know
		     the child node won't go away because we already hold the
		     additional reference to it.  */
		  pthread_mutex_unlock (&dir->lock);
		  pthread_mutex_lock (&(*node)->lock);
		}
	    }
	}
    }
  else
    {
      err = ENOTDIR;
    }

  if (err)
    {
      *node = 0;
      pthread_mutex_unlock (&dir->lock);
    }
  else
    {
      /* Update the node cache */
      node_cache (*node);
    }

  return err;
}

/* Delete NAME in DIR for USER. */
error_t
netfs_attempt_unlink (struct iouser * user, struct node * dir, char *name)
{
  return EOPNOTSUPP;
}

/* Note that in this one call, neither of the specific nodes are locked. */
error_t
netfs_attempt_rename (struct iouser * user, struct node * fromdir,
		      char *fromname, struct node * todir,
		      char *toname, int excl)
{
  return EOPNOTSUPP;
}

/* Attempt to create a new directory named NAME in DIR for USER with mode
   MODE.  */
error_t
netfs_attempt_mkdir (struct iouser * user, struct node * dir,
		     char *name, mode_t mode)
{
  return EOPNOTSUPP;
}

/* Attempt to remove directory named NAME in DIR for USER. */
error_t
netfs_attempt_rmdir (struct iouser * user, struct node * dir, char *name)
{
  return EOPNOTSUPP;
}

/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the owner to UID and the group to GID. */
error_t
netfs_attempt_chown (struct iouser * cred, struct node * node,
		     uid_t uid, uid_t gid)
{
  return EOPNOTSUPP;
}

/* This should attempt a chauthor call for the user specified by CRED on node
   NODE, to change the author to AUTHOR. */
error_t
netfs_attempt_chauthor (struct iouser * cred, struct node * node,
			uid_t author)
{
  return EOPNOTSUPP;
}

/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the mode to MODE.  Unlike the normal Unix and Hurd meaning
   of chmod, this function is also used to attempt to change files into other
   types.  If such a transition is attempted which is impossible, then return
   EOPNOTSUPP.  */
error_t
netfs_attempt_chmod (struct iouser * cred, struct node * node, mode_t mode)
{
  return EOPNOTSUPP;
}

/* Attempt to turn NODE (user CRED) into a symlink with target NAME. */
error_t
netfs_attempt_mksymlink (struct iouser * cred, struct node * node, char *name)
{
  return EOPNOTSUPP;
}

/* Attempt to turn NODE (user CRED) into a device.  TYPE is either S_IFBLK or
   S_IFCHR. */
error_t
netfs_attempt_mkdev (struct iouser * cred, struct node * node,
		     mode_t type, dev_t indexes)
{
  return EOPNOTSUPP;
}

/* This should attempt a chflags call for the user specified by CRED on node
   NODE, to change the flags to FLAGS. */
error_t
netfs_attempt_chflags (struct iouser * cred, struct node * node, int flags)
{
  return EOPNOTSUPP;
}

/* This should attempt to set the size of the file NODE (for user CRED) to
   SIZE bytes long. */
error_t
netfs_attempt_set_size (struct iouser * cred, struct node * node, off_t size)
{
  /* Do nothing */
  return 0;
}

/* This should attempt to fetch filesystem status information for the remote
   filesystem, for the user CRED. */
error_t
netfs_attempt_statfs (struct iouser * cred, struct node * node,
		      struct statfs * st)
{
  memset (st, 0, sizeof *st);
  st->f_type = FSTYPE_PCI;
  st->f_fsid = getpid ();
  return 0;
}

/* This should sync the entire remote filesystem.  If WAIT is set, return
   only after sync is completely finished.  */
error_t
netfs_attempt_syncfs (struct iouser * cred, int wait)
{
  return 0;
}

/* Create a link in DIR with name NAME to FILE for USER.  Note that neither
   DIR nor FILE are locked.  If EXCL is set, do not delete the target, but
   return EEXIST if NAME is already found in DIR.  */
error_t
netfs_attempt_link (struct iouser * user, struct node * dir,
		    struct node * file, char *name, int excl)
{
  return EOPNOTSUPP;
}

/* Attempt to create an anonymous file related to DIR for USER with MODE.
   Set *NODE to the returned file upon success.  No matter what, unlock DIR. */
error_t
netfs_attempt_mkfile (struct iouser * user, struct node * dir,
		      mode_t mode, struct node ** node)
{
  return EOPNOTSUPP;
}

/* Read the contents of NODE (a symlink), for USER, into BUF. */
error_t
netfs_attempt_readlink (struct iouser * user, struct node * node, char *buf)
{
  return EOPNOTSUPP;
}

/* Read from the file NODE for user CRED starting at OFFSET and continuing for
   up to *LEN bytes.  Put the data at DATA.  Set *LEN to the amount
   successfully read upon return.  */
error_t
netfs_attempt_read (struct iouser * cred, struct node * node,
		    off_t offset, size_t * len, void *data)
{
  error_t err;

  if (!strncmp (node->nn->ln->name, FILE_CONFIG_NAME, NAME_SIZE))
    {
      err =
        io_config_file (node->nn->ln->device, offset, len, data,
                        pci_device_cfg_read);
      if (!err)
        /* Update atime */
        UPDATE_TIMES (node->nn->ln, TOUCH_ATIME);
    }
  else if (!strncmp (node->nn->ln->name, FILE_ROM_NAME, NAME_SIZE))
    {
      err = read_rom_file (node->nn->ln->device, offset, len, data);
      if (!err)
	/* Update atime */
	UPDATE_TIMES (node->nn->ln, TOUCH_ATIME);
    }
  else if (!strncmp
	   (node->nn->ln->name, FILE_REGION_NAME, strlen (FILE_REGION_NAME)))
    {
      err = io_region_file (node->nn->ln, offset, len, data, 1);
      if (!err)
	/* Update atime */
	UPDATE_TIMES (node->nn->ln, TOUCH_ATIME);
    }
  else
    return EOPNOTSUPP;

  return err;
}

/* Write to the file NODE for user CRED starting at OFSET and continuing for up
   to *LEN bytes from DATA.  Set *LEN to the amount seccessfully written upon
   return. */
error_t
netfs_attempt_write (struct iouser * cred, struct node * node,
		     off_t offset, size_t * len, void *data)
{
  error_t err;

  if (!strncmp (node->nn->ln->name, FILE_CONFIG_NAME, NAME_SIZE))
    {
      err =
        io_config_file (node->nn->ln->device, offset, len, data,
                       (pci_io_op_t) pci_device_cfg_write);
      if (!err)
        {
          /* Update mtime and ctime */
          UPDATE_TIMES (node->nn->ln, TOUCH_MTIME | TOUCH_CTIME);
        }
    }
  else if (!strncmp
	   (node->nn->ln->name, FILE_REGION_NAME, strlen (FILE_REGION_NAME)))
    {
      err = io_region_file (node->nn->ln, offset, len, data, 0);
      if (!err)
	/* Update atime */
	UPDATE_TIMES (node->nn->ln, TOUCH_MTIME | TOUCH_CTIME);
    }
  else
    return EOPNOTSUPP;

  return err;
}

/* Node NP is all done; free all its associated storage. */
void
netfs_node_norefs (struct node *node)
{
  destroy_node (node);
}
