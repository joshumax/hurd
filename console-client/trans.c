/* trans.c -- Control a translator node for the repeaters.

   Copyright (C) 2004, 2005, 2007 Free Software Foundation, Inc.

   Written by Marco Gerards.
   
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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <fcntl.h>
#include <maptime.h>
#include <stddef.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <hurd/hurd_types.h>
#include <error.h>
#include <version.h>
#include <stdio.h>

#include "trans.h"
#include "libnetfs/io_S.h"


char *netfs_server_name = "console";
char *netfs_server_version = HURD_VERSION;
int netfs_maxsymlinks = 0;

/* Handy source of time.  */
static volatile struct mapped_time_value *console_maptime;

static consnode_t node_list = 0;

struct netnode
{
  consnode_t node;
  char *symlink_path;
};

typedef mach_msg_header_t request_t;


int
console_demuxer (mach_msg_header_t *inp,
		 mach_msg_header_t *outp)
{
  int ret;
  struct protid *user = (struct protid *) inp;
  request_t *inop = (request_t *) inp;

  ret = netfs_demuxer (inp, outp);
  if (ret)
    return ret;

  if (MACH_MSGH_BITS_LOCAL (inp->msgh_bits) ==
      MACH_MSG_TYPE_PROTECTED_PAYLOAD)
    user = ports_lookup_payload (netfs_port_bucket,
				 inop->msgh_protected_payload,
				 netfs_protid_class);
  else
    user = ports_lookup_port (netfs_port_bucket,
			      inop->msgh_local_port,
			      netfs_protid_class);
  if (!user)
    return ret;
  
  /* Don't do anything for the root node.  */
  if (user->po->np == netfs_root_node)
    {
      ports_port_deref (user);
      return 0;
    }    
  
  if (!ret && user->po->np->nn->node && user->po->np->nn->node->demuxer)
    ret = user->po->np->nn->node->demuxer (inp, outp);
  
  ports_port_deref (user);
  return ret;
}



/* Make sure that NP->nn_stat is filled with current information.  CRED
   identifies the user responsible for the operation.  */
error_t
netfs_validate_stat (struct node *np, struct iouser *cred)
{
  return 0;
}


/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the owner to UID and the group to GID. */
error_t
netfs_attempt_chown (struct iouser *cred, struct node *np,
		     uid_t uid, uid_t gid)
{
  return EOPNOTSUPP;
}


/* This should attempt a chauthor call for the user specified by CRED on node
   NODE, to change the author to AUTHOR. */
error_t
netfs_attempt_chauthor (struct iouser *cred, struct node *np,
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
netfs_attempt_chmod (struct iouser *cred, struct node *np,
		     mode_t mode)
{
  return EOPNOTSUPP;
}


/* Attempt to turn NODE (user CRED) into a symlink with target NAME. */
error_t
netfs_attempt_mksymlink (struct iouser *cred, struct node *np,
			 char *name)
{
  if (!np->nn->node)
    {
      if (np->nn->symlink_path)
	free (np->nn->symlink_path);
      np->nn->symlink_path = strdup (name);
      return 0;
    }
  else if (np->nn->node->mksymlink)
    return np->nn->node->mksymlink (cred, np, name);
  return EOPNOTSUPP;
}


/* Attempt to turn NODE (user CRED) into a device.  TYPE is either S_IFBLK or
   S_IFCHR. */
error_t
netfs_attempt_mkdev (struct iouser *cred, struct node *np,
		     mode_t type, dev_t indexes)
{
  return EOPNOTSUPP;
}


/* This should attempt a chflags call for the user specified by CRED on node
   NODE, to change the flags to FLAGS. */
error_t
netfs_attempt_chflags (struct iouser *cred, struct node *np,
		       int flags)
{
  return EOPNOTSUPP;
}


/* This should attempt a utimes call for the user specified by CRED on
   locked node NP, to change the atime to ATIME and the mtime to
   MTIME.  If ATIME or MTIME is null, then set to the current
   time.  */
error_t
netfs_attempt_utimes (struct iouser *cred, struct node *np,
		      struct timespec *atime, struct timespec *mtime)
{
  error_t err = fshelp_isowner (&np->nn_stat, cred);
  int flags = TOUCH_CTIME;
  
  if (! err)
    {
      if (mtime)
        np->nn_stat.st_mtim = *mtime;
      
      if (atime)
        np->nn_stat.st_atim = *atime;
      
      fshelp_touch (&np->nn_stat, flags, console_maptime);
    }
  return err;

}


/* This should attempt to set the size of the locked file NP (for user
   CRED) to SIZE bytes long.  */
error_t
netfs_attempt_set_size (struct iouser *cred, struct node *np,
			loff_t size)
{
  return EOPNOTSUPP;
}

/* This should attempt to fetch filesystem status information for the
   remote filesystem, for the user CRED. NP is locked.  */
error_t
netfs_attempt_statfs (struct iouser *cred, struct node *np,
		      fsys_statfsbuf_t *st)
{
  return EOPNOTSUPP;
}


/* This should sync the locked file NP completely to disk, for the
   user CRED.  If WAIT is set, return only after the sync is
   completely finished.  */
error_t
netfs_attempt_sync (struct iouser *cred, struct node *np,
		    int wait)
{
  return 0;
}


/* This should sync the entire remote filesystem.  If WAIT is set,
   return only after the sync is completely finished.  */
error_t
netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  return 0;
}


/* Lookup NAME in DIR (which is locked) for USER; set *NP to the found
   name upon return.  If the name was not found, then return ENOENT.
   On any error, clear *NP.  (*NP, if found, should be locked and a
   reference to it generated.  This call should unlock DIR no matter
   what.)  */
error_t
netfs_attempt_lookup (struct iouser *user, struct node *dir,
		      char *name, struct node **node)
{
  error_t err;
  consnode_t cn;
  
  *node = 0;
  err = fshelp_access (&dir->nn_stat, S_IEXEC, user);
  if (err)
    goto out;

  if (strcmp (name, ".") == 0)
    {
      /* Current directory -- just add an additional reference to DIR
	 and return it.  */
      netfs_nref (dir);
      *node = dir;
      goto out;
    }

  if (strcmp (name, "..") == 0)
    {
      err = EAGAIN;
      goto out;
    }
  
  for (cn = node_list; cn; cn = cn->next)
    if (!strcmp (name, cn->name))
      {
	if (cn->node == NULL)
	  {
	    struct netnode *nn;
	    ssize_t size = 0;

	    if (cn->readlink)
	      {
		size = cn->readlink (user, NULL, NULL);
		if (size < 0)
		  {
		    err = -size;
		    goto out;
		  }
	      }

	    nn = calloc (1, sizeof *nn);
	    if (nn == NULL)
	      {
		err = ENOMEM;
		goto out;
	      }
	    
	    *node = netfs_make_node (nn);
	    
	    nn->node = cn;
	    (*node)->nn_stat = netfs_root_node->nn_stat;
	    (*node)->nn_stat.st_mode = (netfs_root_node->nn_stat.st_mode & ~S_IFMT & ~S_ITRANS);
	    (*node)->nn_stat.st_ino = 5;
	    if (cn->readlink)
		(*node)->nn_stat.st_mode |= S_IFLNK;
	    else
		(*node)->nn_stat.st_mode |= S_IFCHR;
	    (*node)->nn_stat.st_size = size;
	    cn->node = *node;
	    goto out;
	  }
	else
	  {
	    *node = cn->node;
	    
	    netfs_nref (*node);
	    goto out;
	  }
      }
  
  err = ENOENT;
  
 out:
  pthread_mutex_unlock (&dir->lock);
  if (err)
    *node = 0;
  else
    pthread_mutex_lock (&(*node)->lock);
  
  if (!err && *node != dir && (*node)->nn->node->open)
    (*node)->nn->node->open ();

  return err;
}


error_t
netfs_S_io_seek (struct protid *user, off_t offset,
		 int whence, off_t *newoffset)
{
  /* XXX: Will all nodes be device nodes?  */
  if (!user)
    return EOPNOTSUPP;
  else
    return ESPIPE;
}


static error_t
io_select_common (struct protid *user, mach_port_t reply,
		  mach_msg_type_name_t replytype,
		  struct timespec *tsp, int *type)
{
  struct node *np;
  
  if (!user)
    return EOPNOTSUPP;
  
  np = user->po->np;
  
  if (np->nn->node && np->nn->node->select)
    return np->nn->node->select (user, reply, replytype, tsp, type);
  return EOPNOTSUPP;
}


error_t
netfs_S_io_select (struct protid *user, mach_port_t reply,
		   mach_msg_type_name_t replytype, int *type)
{
  return io_select_common (user, reply, replytype, NULL, type);
}


error_t
netfs_S_io_select_timeout (struct protid *user, mach_port_t reply,
			   mach_msg_type_name_t replytype,
			   struct timespec ts, int *type)
{
  return io_select_common (user, reply, replytype, &ts, type);
}


/* Delete NAME in DIR (which is locked) for USER.  */
error_t
netfs_attempt_unlink (struct iouser *user, struct node *dir,
		      char *name)
{
  error_t err;
  consnode_t cn;

  err = fshelp_access (&dir->nn_stat, S_IWRITE, user);
  if (err)
    return err;

  for (cn = node_list; cn; cn = cn->next)
    if (!strcmp (name, cn->name))
      {
	if (cn->mksymlink)
	  return 0;
	else
	  break;
      }
  return EOPNOTSUPP;
}


/* Attempt to rename the directory FROMDIR to TODIR. Note that neither
   of the specific nodes are locked.  */
error_t
netfs_attempt_rename (struct iouser *user, struct node *fromdir,
		      char *fromname, struct node *todir,
		      char *toname, int excl)
{
  return EOPNOTSUPP;
}


/* Attempt to create a new directory named NAME in DIR (which is
   locked) for USER with mode MODE. */
error_t
netfs_attempt_mkdir (struct iouser *user, struct node *dir,
		     char *name, mode_t mode)
{
  return EOPNOTSUPP;
}


/* Attempt to remove directory named NAME in DIR (which is locked) for
   USER.  */
error_t
netfs_attempt_rmdir (struct iouser *user,
		     struct node *dir, char *name)
{
  return EOPNOTSUPP;
}


/*  Create a link in DIR with name NAME to FILE for USER. Note that
   neither DIR nor FILE are locked. If EXCL is set, do not delete the
   target.  Return EEXIST if NAME is already found in DIR.  */
error_t
netfs_attempt_link (struct iouser *user, struct node *dir,
		    struct node *file, char *name, int excl)
{
  error_t err;
  consnode_t cn;

  err = fshelp_access (&dir->nn_stat, S_IWRITE, user);
  if (err)
    return err;

  if (!file->nn->node && file->nn->symlink_path)
    {
      for (cn = node_list; cn; cn = cn->next)
	if (!strcmp (name, cn->name))
	  {
	    if (cn->mksymlink)
	      {
		file->nn->node = cn;
		cn->mksymlink (user, file, file->nn->symlink_path);
		free (file->nn->symlink_path);
		file->nn->symlink_path = NULL;
		return 0;
	      }
	    else
	      break;
	  }
    }
  return EOPNOTSUPP;
}


/* Attempt to create an anonymous file related to DIR (which is
   locked) for USER with MODE.  Set *NP to the returned file upon
   success. No matter what, unlock DIR.  */
error_t
netfs_attempt_mkfile (struct iouser *user, struct node *dir,
			     mode_t mode, struct node **np)
{
  error_t err;
  struct netnode *nn;

  err = fshelp_access (&dir->nn_stat, S_IWRITE, user);
  if (err)
    {
      *np = 0;
      return err;
    }

  pthread_mutex_unlock (&dir->lock);

  nn = calloc (1, sizeof (*nn));
  if (!nn)
    return ENOMEM;

  *np = netfs_make_node (nn);
  pthread_mutex_lock (&(*np)->lock);

  return 0;
}


/* Attempt to create a file named NAME in DIR (which is locked) for
   USER with MODE.  Set *NP to the new node upon return.  On any
   error, clear *NP.  *NP should be locked on success; no matter what,
   unlock DIR before returning.  */
error_t
netfs_attempt_create_file (struct iouser *user, struct node *dir,
			   char *name, mode_t mode, struct node **np)
{
  *np = 0;
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}


/* Read the contents of locked node NP (a symlink), for USER, into
   BUF.  */
error_t
netfs_attempt_readlink (struct iouser *user, struct node *np,
			char *buf)
{
  if (np->nn->node && np->nn->node->readlink)
  {
    error_t err = np->nn->node->readlink (user, np, buf);
    if (err < 0)
      return -err;
    else
      return 0;
  }
  return EOPNOTSUPP;
}


/* Locked node NP is being opened by USER, with FLAGS.  NEWNODE is
   nonzero if we just created this node.  Return an error if we should
   not permit the open to complete because of a permission
   restriction.  */
error_t
netfs_check_open_permissions (struct iouser *user, struct node *np,
			      int flags, int newnode)
{
  error_t err = 0;
  
  if (flags & O_READ)
    err = fshelp_access (&np->nn_stat, S_IREAD, user);
  if (!err && (flags & O_WRITE))
    err = fshelp_access (&np->nn_stat, S_IWRITE, user);
  if (!err && (flags & O_EXEC))
    err = fshelp_access (&np->nn_stat, S_IEXEC, user);
  return err;

}


/* This function will never be called.  It is only used when a node is
   a symlink or by io_read, which is overridden.  */
error_t
netfs_attempt_read (struct iouser *cred, struct node *np,
		    loff_t offset, size_t *len, void *data)
{
  return EOPNOTSUPP;
}


/* This function will never be called.  It is only called from
   io_write, which is overridden.  */
error_t
netfs_attempt_write (struct iouser *cred, struct node *np,
		     loff_t offset, size_t *len, void *data)
{
  return EOPNOTSUPP;
}


error_t
netfs_S_io_read (struct protid *user,
		 data_t *data,
		 mach_msg_type_number_t *datalen,
		 off_t offset,
		 mach_msg_type_number_t amount)
{
  struct node *np;
  
  if (!user)
    return EOPNOTSUPP;
  np = user->po->np;
  
  if (np->nn->node && np->nn->node->read)
    return np->nn->node->read (user, data, datalen, offset, amount);
  return EOPNOTSUPP;
}


error_t
netfs_S_io_write (struct protid *user,
		  data_t data,
		  mach_msg_type_number_t datalen,
		  off_t offset,
		  mach_msg_type_number_t *amount)
{
  struct node *np;
  
  if (!user)
    return EOPNOTSUPP;
  
  np = user->po->np;
  if (np->nn->node && np->nn->node->write)
    return np->nn->node->write (user, data, datalen, offset, amount);
  return EOPNOTSUPP;
}


/* Return the valid access types (bitwise OR of O_READ, O_WRITE, and
   O_EXEC) in *TYPES for locked file NP and user CRED.  */
error_t
netfs_report_access (struct iouser *cred, struct node *np,
		     int *types)
{
  *types = 0;
  if (fshelp_access (&np->nn_stat, S_IREAD, cred) == 0)
    *types |= O_READ;
  if (fshelp_access (&np->nn_stat, S_IWRITE, cred) == 0)
    *types |= O_WRITE;
  if (fshelp_access (&np->nn_stat, S_IEXEC, cred) == 0)
    *types |= O_EXEC;
  return 0;

}

/* Node NP has no more references; free all its associated storage. */
void netfs_node_norefs (struct node *np)
     
{
  if (np->nn->node)
    {
      if (np->nn->node->close)
	np->nn->node->close ();
      np->nn->node->node = 0;
    }

  if (np->nn->symlink_path)
    free (np->nn->symlink_path);

  free (np->nn);
  free (np);
}


/* Returned directory entries are aligned to blocks this many bytes long.
   Must be a power of two.  */
#define DIRENT_ALIGN 4
#define DIRENT_NAME_OFFS offsetof (struct dirent, d_name)
/* Length is structure before the name + the name + '\0', all
   padded to a four-byte alignment.  */
#define DIRENT_LEN(name_len)                                                  \
  ((DIRENT_NAME_OFFS + (name_len) + 1 + (DIRENT_ALIGN - 1))                   \
   & ~(DIRENT_ALIGN - 1))

/* Fill the array *DATA of size BUFSIZE with up to NENTRIES dirents
   from DIR (which is locked) starting with entry ENTRY for user CRED.
   The number of entries in the array is stored in *AMT and the number
   of bytes in *DATACNT.  If the supplied buffer is not large enough
   to hold the data, it should be grown.  */
error_t
netfs_get_dirents (struct iouser *cred, struct node *dir,
		   int first_entry, int num_entries, char **data,
		   mach_msg_type_number_t *data_len,
		   vm_size_t max_data_len, int *data_entries)
{
  error_t err;
  int count = 0;
  size_t size = 0;              /* Total size of our return block.  */
  consnode_t cn = node_list;
  consnode_t first_node;
  

  /* Add the length of a directory entry for NAME to SIZE and return true,
     unless it would overflow MAX_DATA_LEN or NUM_ENTRIES, in which case
     return false.  */
  int bump_size (const char *name)
    {
      if (num_entries == -1 || count < num_entries)
        {
          size_t new_size = size + DIRENT_LEN (strlen (name));
          if (max_data_len > 0 && new_size > max_data_len)
            return 0;
          size = new_size;
          count++;
          return 1;
        }
      else
        return 0;
    }

  if (dir != netfs_root_node)
    return ENOTDIR;

  for (first_node = node_list, count = 2;
       first_node && first_entry > count;
       first_node = first_node->next);
  count++;

  count = 0;

  /* Make space for the `.' and `..' entries.  */
  if (first_entry == 0)
    bump_size (".");
  if (first_entry <= 1)
    bump_size ("..");

  for (cn = first_node; cn; cn = cn->next)
    bump_size (cn->name);
  
  
  /* Allocate it.  */
  *data = mmap (0, size, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  err = ((void *) *data == (void *) -1) ? errno : 0;

  if (! err)
    /* Copy out the result.  */
    {
      char *p = *data;

      int add_dir_entry (const char *name, ino_t fileno, int type)
        {
          if (num_entries == -1 || count < num_entries)
            {
              struct dirent hdr;
              size_t name_len = strlen (name);
              size_t sz = DIRENT_LEN (name_len);

              if (sz > size)
                return 0;
              else
                size -= sz;

              hdr.d_fileno = fileno;
              hdr.d_reclen = sz;
              hdr.d_type = type;
              hdr.d_namlen = name_len;

              memcpy (p, &hdr, DIRENT_NAME_OFFS);
              strcpy (p + DIRENT_NAME_OFFS, name);
              p += sz;

              count++;

              return 1;
            }
          else
            return 0;
        }

      *data_len = size;
      *data_entries = count;
      
      count = 0;

      /* Add `.' and `..' entries.  */
      if (first_entry == 0)
	add_dir_entry (".", 2, DT_DIR);
      if (first_entry <= 1)
	add_dir_entry ("..", 2, DT_DIR);
      
      /* Fill in the real directory entries.  */
      for (cn = first_node; cn; cn = cn->next)
	if (!add_dir_entry (cn->name, cn->id, cn->readlink ? DT_LNK : DT_CHR))
	  break;
    }
      
  fshelp_touch (&dir->nn_stat, TOUCH_ATIME, console_maptime);
  return err;
}



static void *
console_client_translator (void *unused)
{
  error_t err;

  do 
    {
      ports_manage_port_operations_multithread (netfs_port_bucket,
						console_demuxer,
						1000 * 60 * 2,
						1000 * 60 * 10,
						0);
      err = netfs_shutdown (0);
    }
  while (err);
  return 0;
}


/* Create a node with the name NAME and return it in *CN.  */
error_t
console_create_consnode (const char *name, consnode_t *cn)
{
  *cn = malloc (sizeof (struct consnode));
  if (!*cn)
    return ENOMEM;
  
  (*cn)->name = strdup (name);
  if (!(*cn)->name)
    {
      free (cn);
      return ENOMEM;
    }

  (*cn)->readlink = NULL;
  (*cn)->mksymlink = NULL;

  return 0;
}


/* Destroy the node CN.  */
void
console_destroy_consnode (consnode_t cn)
{
  if (!cn)
    return;
  free (cn->name);
  free (cn);
}


/* Register the node CN with the translator.  */
void
console_register_consnode (consnode_t cn)
{
  cn->node = 0;
  cn->next = node_list;
  node_list = cn;
}


/* Unregister the node CN from the translator.  */
void
console_unregister_consnode (consnode_t cn)
{
  if (!cn)
    return;
  
  if (node_list == cn)
    node_list = cn->next;
  else
    {
      consnode_t prev = node_list;
      
      for (prev = node_list; prev->next != cn; prev = prev->next)
	;
      
      prev->next = cn->next;
    }
}


error_t
console_setup_node (char *path)
{
  error_t err;
  struct stat ul_stat;
  file_t node;
  struct port_info *newpi;
  mach_port_t right;
  pthread_t thread;
  
  node = file_name_lookup (path, O_CREAT|O_NOTRANS, 0664);
  if (node == MACH_PORT_NULL)
    return errno;

  netfs_init ();
  
  /* Create the root node (some attributes initialized below).  */
  netfs_root_node = netfs_make_node (0);
  if (! netfs_root_node)
    error (1, ENOMEM, "Cannot create root node");
  
  err = maptime_map (0, 0, &console_maptime);
  if (err)
    error (1, err, "Cannot map time");
  
  err = ports_create_port (netfs_control_class, netfs_port_bucket, sizeof (struct port_info), &newpi);
  right = ports_get_send_right (newpi);
  err = file_set_translator (node, 0, FS_TRANS_EXCL | FS_TRANS_SET, 0, 0, 0,
			     right, MACH_MSG_TYPE_COPY_SEND); 
  mach_port_deallocate (mach_task_self (), right);
  
  err = io_stat (node, &ul_stat);
  if (err)
    error (1, err, "Cannot stat underlying node");
  
  netfs_root_node->nn_stat.st_ino = 2;
  netfs_root_node->nn_stat.st_uid = ul_stat.st_uid;
  netfs_root_node->nn_stat.st_gid = ul_stat.st_gid;
  netfs_root_node->nn_stat.st_author = ul_stat.st_author;
  netfs_root_node->nn_stat.st_mode = S_IFDIR | (ul_stat.st_mode & ~S_IFMT & ~S_ITRANS);
  netfs_root_node->nn_stat.st_fsid = getpid ();
  netfs_root_node->nn_stat.st_nlink = 1;
  netfs_root_node->nn_stat.st_size = 0;
  netfs_root_node->nn_stat.st_blocks = 0;
  netfs_root_node->nn_stat.st_fstype = FSTYPE_MISC;
  netfs_root_node->nn_translated = 0;

  /* If the underlying node isn't a directory, propagate read permission to
     execute permission since we need that for lookups.  */
  if (! S_ISDIR (ul_stat.st_mode))
    {
      if (ul_stat.st_mode & S_IRUSR)
        netfs_root_node->nn_stat.st_mode |= S_IXUSR;
      if (ul_stat.st_mode & S_IRGRP)
        netfs_root_node->nn_stat.st_mode |= S_IXGRP;
      if (ul_stat.st_mode & S_IROTH)
        netfs_root_node->nn_stat.st_mode |= S_IXOTH;
    }
      
  fshelp_touch (&netfs_root_node->nn_stat, TOUCH_ATIME|TOUCH_MTIME|TOUCH_CTIME,
                console_maptime);
  
  err = pthread_create (&thread, NULL, console_client_translator, NULL);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }

  return 0;
}
