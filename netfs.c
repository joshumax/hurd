/* procfs -- a translator for providing GNU/Linux compatible 
             proc pseudo-filesystem

   Copyright (C) 2008, FSF.
   Written as a Summer of Code Project
   
   procfs is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   procfs is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. 
*/

#include <stddef.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <hurd/netfs.h>

#include "procfs.h"

/* Trivial definitions.  */

/* Make sure that NP->nn_stat is filled with current information.  CRED
   identifies the user responsible for the operation.  */
error_t
netfs_validate_stat (struct node *node, struct iouser *cred)
{
  return procfs_refresh_node (node);
}

/* This should sync the file NODE completely to disk, for the user CRED.  If
   WAIT is set, return only after sync is completely finished.  */
error_t
netfs_attempt_sync (struct iouser *cred, struct node *node, int wait)
{
  return 0;
}

/* Attempt to create a new directory named NAME in DIR for USER with mode
   MODE.  */
error_t netfs_attempt_mkdir (struct iouser *user, struct node *dir,
			     char *name, mode_t mode)
{
  return EROFS;
}

/* Attempt to remove directory named NAME in DIR for USER. */
error_t netfs_attempt_rmdir (struct iouser *user,
			     struct node *dir, char *name)
{
  return EROFS;
}

/* Attempt to set the passive translator record for FILE to ARGZ (of length
   ARGZLEN) for user CRED. */
error_t netfs_set_translator (struct iouser *cred, struct node *node,
			      char *argz, size_t argzlen)
{
  return EROFS;
}

/* Attempt to create a file named NAME in DIR for USER with MODE.  Set *NODE
   to the new node upon return.  On any error, clear *NODE.  *NODE should be
   locked on success; no matter what, unlock DIR before returning.  */
error_t
netfs_attempt_create_file (struct iouser *user, struct node *dir,
			   char *name, mode_t mode, struct node **node)
{
  *node = NULL;
  mutex_unlock (&dir->lock);
  return EROFS;
}

/* Node NODE is being opened by USER, with FLAGS.  NEWNODE is nonzero if we
   just created this node.  Return an error if we should not permit the open
   to complete because of a permission restriction. */
error_t
netfs_check_open_permissions (struct iouser *user, struct node *node,
			      int flags, int newnode)
{
  error_t err = procfs_refresh_node (node);
  if (!err && (flags & O_READ))
    err = fshelp_access (&node->nn_stat, S_IREAD, user);
  if (!err && (flags & O_WRITE))
    err = fshelp_access (&node->nn_stat, S_IWRITE, user);
  if (!err && (flags & O_EXEC))
    err = fshelp_access (&node->nn_stat, S_IEXEC, user);
  return err;
}

/* This should attempt a utimes call for the user specified by CRED on node
   NODE, to change the atime to ATIME and the mtime to MTIME. */
error_t
netfs_attempt_utimes (struct iouser *cred, struct node *node,
		      struct timespec *atime, struct timespec *mtime)
{
  error_t err = procfs_refresh_node (node);
  int flags = TOUCH_CTIME;

  if (! err)
    err = fshelp_isowner (&node->nn_stat, cred);

  if (! err)
    {
      if (atime)
	node->nn_stat.st_atim = *atime;
      else
	flags |= TOUCH_ATIME;

      if (mtime)
	node->nn_stat.st_mtim = *mtime;
      else
	flags |= TOUCH_MTIME;

      fshelp_touch (&node->nn_stat, flags, procfs_maptime);
    }

  return err;
}

/* Return the valid access types (bitwise OR of O_READ, O_WRITE, and O_EXEC)
   in *TYPES for file NODE and user CRED.  */
error_t
netfs_report_access (struct iouser *cred, struct node *node, int *types)
{
  error_t err = procfs_refresh_node (node);

  if (! err)
    {
      *types = 0;
      if (fshelp_access (&node->nn_stat, S_IREAD, cred) == 0)
	*types |= O_READ;
      if (fshelp_access (&node->nn_stat, S_IWRITE, cred) == 0)
	*types |= O_WRITE;
      if (fshelp_access (&node->nn_stat, S_IEXEC, cred) == 0)
	*types |= O_EXEC;
    }

  return err;
}

/* The granularity with which we allocate space to return our result.  */
#define DIRENTS_CHUNK_SIZE	(8*1024)

/* Returned directory entries are aligned to blocks this many bytes long.
   Must be a power of two.  */
#define DIRENT_ALIGN 4
#define DIRENT_NAME_OFFS offsetof (struct dirent, d_name)

/* Length is structure before the name + the name + '\0', all
   padded to a four-byte alignment.  */
#define DIRENT_LEN(name_len)						      \
  ((DIRENT_NAME_OFFS + (name_len) + 1 + (DIRENT_ALIGN - 1))		      \
   & ~(DIRENT_ALIGN - 1))



/* Fetch a directory  */
error_t
netfs_get_dirents (struct iouser *cred, struct node *dir,
		 int first_entry, int max_entries, char **data,
		 mach_msg_type_number_t *data_len,
		 vm_size_t max_data_len, int *data_entries)
{
  error_t err = procfs_refresh_node (dir);
  struct procfs_dir_entry *dir_entry;

  if (! err)
    {
      if (dir->nn->dir) 
        {
          if (! procfs_dir_refresh (dir->nn->dir, dir == dir->nn->fs->root))
            {
              for (dir_entry = dir->nn->dir->ordered; first_entry > 0 &&
                  dir_entry; first_entry--,
                  dir_entry = dir_entry->ordered_next);
              if (! dir_entry )
                max_entries = 0;
                
              if (max_entries != 0)
                {  
                  size_t size = 0;
                  char *p;
                  int count = 0;

             
                  if (max_data_len == 0) 
                    size = DIRENTS_CHUNK_SIZE;
                  else if (max_data_len > DIRENTS_CHUNK_SIZE)
                    size = DIRENTS_CHUNK_SIZE;
                  else 
                    size = max_data_len;  
                
                  *data = mmap (0, size, PROT_READ|PROT_WRITE,
		            MAP_ANON, 0, 0);
		             
	          err = ((void *) *data == (void *) -1) ? errno : 0;
	       
	          if (! err)
	            {
	              p = *data;
	           
	              /* This gets all the actual entries present.  */

                      while ((max_entries == -1 || count < max_entries) && dir_entry)
                        {
                          struct dirent hdr;
                          size_t name_len = strlen (dir_entry->name);
                          size_t sz = DIRENT_LEN (name_len);
                          int entry_type = IFTODT (dir_entry->stat.st_mode);

                          if ((p - *data) + sz > size)
       		            {
		              if (max_data_len > 0)
		                break;
		              else   /* The Buffer Size must be increased. */
                                {
		                  vm_address_t extension = (vm_address_t)(*data + size);
		                  err = vm_allocate (mach_task_self (), &extension,
					 DIRENTS_CHUNK_SIZE, 0);
					 
		                  if (err)
			            break;
			           
		                  size += DIRENTS_CHUNK_SIZE;
		                }
		            }

	                  hdr.d_namlen = name_len;
                          hdr.d_fileno = dir_entry->stat.st_ino;
	                  hdr.d_reclen = sz;
	                  hdr.d_type = entry_type;

	                  memcpy (p, &hdr, DIRENT_NAME_OFFS);
	                  strcpy (p + DIRENT_NAME_OFFS, dir_entry->name);

	                  p += sz;

                          count++;
	                  dir_entry = dir_entry->ordered_next;
                        }
                        
                      if (err)
                        munmap (*data, size);
                      else
                        {
	                  vm_address_t alloc_end = (vm_address_t)(*data + size);
	                  vm_address_t real_end = round_page (p);
	                  if (alloc_end > real_end)
		            munmap ((caddr_t) real_end, alloc_end - real_end);
	                  *data_len = p - *data;
	                  *data_entries = count;
	                }  
	            }
                }
              else
                {
                  *data_len = 0;
                  *data_entries = 0;
                }
            }
        }
      else
        return ENOTDIR;          
    }      
    
  procfs_dir_entries_remove (dir->nn->dir);
  return err;
}		 

/* Lookup NAME in DIR for USER; set *NODE to the found name upon return.  If
   the name was not found, then return ENOENT.  On any error, clear *NODE.
   (*NODE, if found, should be locked, this call should unlock DIR no matter
   what.) */
error_t netfs_attempt_lookup (struct iouser *user, struct node *dir,
			      char *name, struct node **node)
{
  error_t err = procfs_refresh_node (dir);

  if (! err)
    err = procfs_dir_lookup (dir->nn->dir, name, node);

  return err;
}

/* Delete NAME in DIR for USER. */
error_t netfs_attempt_unlink (struct iouser *user, struct node *dir,
			      char *name)
{
  return EROFS;
}

/* Note that in this one call, neither of the specific nodes are locked. */
error_t netfs_attempt_rename (struct iouser *user, struct node *fromdir,
			      char *fromname, struct node *todir,
			      char *toname, int excl)
{
  return EROFS;
}

/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the owner to UID and the group to GID. */
error_t netfs_attempt_chown (struct iouser *cred, struct node *node,
			     uid_t uid, uid_t gid)
{
  return EROFS;
}

/* This should attempt a chauthor call for the user specified by CRED on node
   NODE, to change the author to AUTHOR. */
error_t netfs_attempt_chauthor (struct iouser *cred, struct node *node,
				uid_t author)
{
  return EROFS;
}

/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the mode to MODE.  Unlike the normal Unix and Hurd meaning
   of chmod, this function is also used to attempt to change files into other
   types.  If such a transition is attempted which is impossible, then return
   EOPNOTSUPP.  */
error_t netfs_attempt_chmod (struct iouser *cred, struct node *node,
			     mode_t mode)
{
  return EROFS;
}

/* Attempt to turn NODE (user CRED) into a symlink with target NAME. */
error_t netfs_attempt_mksymlink (struct iouser *cred, struct node *node,
				 char *name)
{
  return EROFS;
}

/* Attempt to turn NODE (user CRED) into a device.  TYPE is either S_IFBLK or
   S_IFCHR. */
error_t netfs_attempt_mkdev (struct iouser *cred, struct node *node,
			     mode_t type, dev_t indexes)
{
  return EROFS;
}


/* This should attempt a chflags call for the user specified by CRED on node
   NODE, to change the flags to FLAGS. */
error_t netfs_attempt_chflags (struct iouser *cred, struct node *node,
			       int flags)
{
  return EROFS;
}

/* This should attempt to set the size of the file NODE (for user CRED) to
   SIZE bytes long. */
error_t netfs_attempt_set_size (struct iouser *cred, struct node *node,
				off_t size)
{
  return EROFS;
}

/* This should attempt to fetch filesystem status information for the remote
   filesystem, for the user CRED. */
error_t
netfs_attempt_statfs (struct iouser *cred, struct node *node,
		      struct statfs *st)
{
  bzero (st, sizeof *st);
  st->f_type = PROCFILESYSTEM;
  st->f_fsid = getpid ();
  return 0;
}

/* This should sync the entire remote filesystem.  If WAIT is set, return
   only after sync is completely finished.  */
error_t netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  return 0;
}

/* Create a link in DIR with name NAME to FILE for USER.  Note that neither
   DIR nor FILE are locked.  If EXCL is set, do not delete the target, but
   return EEXIST if NAME is already found in DIR.  */
error_t netfs_attempt_link (struct iouser *user, struct node *dir,
			    struct node *file, char *name, int excl)
{
  return EROFS;
}

/* Attempt to create an anonymous file related to DIR for USER with MODE.
   Set *NODE to the returned file upon success.  No matter what, unlock DIR. */
error_t netfs_attempt_mkfile (struct iouser *user, struct node *dir,
			      mode_t mode, struct node **node)
{
  *node = NULL;
  mutex_unlock (&dir->lock);
  return EROFS;
}

/* Read the contents of NODE (a symlink), for USER, into BUF. */
error_t netfs_attempt_readlink (struct iouser *user, struct node *node, char *buf)
{
  error_t err = procfs_refresh_node (node);
  if (! err)
    {
      struct procfs_dir_entry *dir_entry = node->nn->dir_entry;
      if (dir_entry)
	bcopy (dir_entry->symlink_target, buf, node->nn_stat.st_size);
      else
	err = EINVAL;
    }
  return err;
}

/* Read from the file NODE for user CRED starting at OFFSET and continuing for
   up to *LEN bytes.  Put the data at DATA.  Set *LEN to the amount
   successfully read upon return.  */
error_t netfs_attempt_read (struct iouser *cred, struct node *node,
			    off_t offset, size_t *len, void *data)
{
  error_t err;
  err = procfs_refresh_node (node);

  if (! err)
    {
      if (*len > 0)
        procfs_read_files_contents (node, offset,
                                    len, data);
      if (*len > 0)
        if (offset >= *len)
          *len = 0;
    }

  return err;
}

/* Write to the file NODE for user CRED starting at OFFSET and continuing for up
   to *LEN bytes from DATA.  Set *LEN to the amount seccessfully written upon
   return. */
error_t netfs_attempt_write (struct iouser *cred, struct node *node,
			     off_t offset, size_t *len, void *data)
{
  return EROFS;
}

/* The user must define this function.  Node NP is all done; free
   all its associated storage. */
void netfs_node_norefs (struct node *np)
{
	mutex_lock (&np->lock);
	*np->prevp = np->next;
	np->next->prevp = np->prevp;
	procfs_remove_node (np);
}

