/* Socket I/O operations

   Copyright (C) 2016 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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

#include <fcntl.h>

#include "sock.h"
#include "sserver.h"

#include "fs_S.h"

error_t
S_dir_notice_changes (struct sock_user *cred,
		      mach_port_t notify)
{
  return EOPNOTSUPP;
}

error_t
S_dir_link (struct sock_user *dircred,
	    struct sock_user *filecred,
	    char *name,
	    int excl)
{
  return EOPNOTSUPP;
}

error_t
S_dir_lookup (struct sock_user *dircred,
	      char *path,
	      int flags,
	      mode_t mode,
	      enum retry_type *retry,
	      char *retryname,
	      file_t *returned_port,
	      mach_msg_type_name_t *returned_port_poly)
{
  return EOPNOTSUPP;
}

error_t
S_dir_mkdir (struct sock_user *dircred,
	     char *name,
	     mode_t mode)
{
  return EOPNOTSUPP;
}

error_t
S_dir_mkfile (struct sock_user *cred,
	      int flags,
	      mode_t mode,
	      mach_port_t *newnode,
	      mach_msg_type_name_t *newnodetype)
{
  return EOPNOTSUPP;
}

error_t
S_dir_readdir (struct sock_user *cred,
	       char **data,
	       size_t *datacnt,
	       boolean_t *data_dealloc,
	       int entry,
	       int nentries,
	       vm_size_t bufsiz,
	       int *amt)
{
  return EOPNOTSUPP;
}

error_t
S_dir_rename (struct sock_user *fromcred,
	      char *fromname,
	      struct sock_user *tocred,
	      char *toname,
	      int excl)
{
  return EOPNOTSUPP;
}

error_t
S_dir_rmdir (struct sock_user *dircred,
	     char *name)
{
  return EOPNOTSUPP;
}

error_t
S_dir_unlink (struct sock_user *dircred,
	      char *name)
{
  return EOPNOTSUPP;
}

error_t
S_file_chauthor (struct sock_user *user,
		 uid_t author)
{
  return EOPNOTSUPP;
}

error_t
S_file_check_access (struct sock_user *cred, int *type)
{
  if (!cred)
    return EOPNOTSUPP;

  *type = 0;
  if (cred->sock->read_pipe)
    *type |= O_READ;
  if (cred->sock->write_pipe)
    *type |= O_WRITE;

  return 0;
}

error_t
S_file_chflags (struct sock_user *cred, int flags)
{
  return EOPNOTSUPP;
}

error_t
S_file_notice_changes (struct sock_user *cred, mach_port_t notify)
{
  return EOPNOTSUPP;
}

error_t
S_file_chmod (struct sock_user *cred, mode_t mode)
{
  return EOPNOTSUPP;
}

error_t
S_file_chown (struct sock_user *cred, uid_t uid, gid_t gid)
{
  return EOPNOTSUPP;
}

error_t
S_file_exec (struct sock_user *cred,
	     task_t task,
	     int flags,
	     char *argv,
	     size_t argvlen,
	     char *envp,
	     size_t envplen,
	     mach_port_t *fds,
	     size_t fdslen,
	     mach_port_t *portarray,
	     size_t portarraylen,
	     int *intarray,
	     size_t intarraylen,
	     mach_port_t *deallocnames,
	     size_t deallocnameslen,
	     mach_port_t *destroynames,
	     size_t destroynameslen)
{
  return EOPNOTSUPP;
}

error_t
S_file_get_children (struct sock_user *cred,
		     char **children,
		     mach_msg_type_number_t *children_len)
{
  return EOPNOTSUPP;
}

error_t
S_file_getcontrol (struct sock_user *cred,
		   mach_port_t *control,
		   mach_msg_type_name_t *controltype)
{
  return EOPNOTSUPP;
}

error_t
S_file_getfh (struct sock_user *cred, char **fh, size_t *fh_len)
{
  return EOPNOTSUPP;
}

error_t
S_file_get_fs_options (struct sock_user *cred, char **data, size_t *data_len)
{
  return EOPNOTSUPP;
}

error_t
S_file_getlinknode (struct sock_user *cred,
		    file_t *port,
		    mach_msg_type_name_t *portpoly)
{
  return EOPNOTSUPP;
}

error_t
S_file_get_source (struct sock_user *cred, char *source)
{
  return EOPNOTSUPP;
}

error_t
S_file_get_storage_info (struct sock_user *cred,
			 mach_port_t **ports,
			 mach_msg_type_name_t *ports_type,
			 mach_msg_type_number_t *num_ports,
			 int **ints, mach_msg_type_number_t *num_ints,
			 off_t **offsets,
			 mach_msg_type_number_t *num_offsets,
			 char **data, mach_msg_type_number_t *data_len)
{
  return EOPNOTSUPP;
}

error_t
S_file_get_translator (struct sock_user *cred, char **trans, size_t *translen)
{
  return EOPNOTSUPP;
}

error_t
S_file_get_translator_cntl (struct sock_user *cred,
			    mach_port_t *ctl,
			    mach_msg_type_name_t *ctltype)
{
  return EOPNOTSUPP;
}

error_t
S_file_lock (struct sock_user *cred, int flags)
{
  return EOPNOTSUPP;
}

error_t
S_file_lock_stat (struct sock_user *cred, int *mystatus, int *otherstatus)
{
  return EOPNOTSUPP;
}

error_t
S_file_reparent (struct sock_user *cred, mach_port_t parent,
		 mach_port_t *new, mach_msg_type_name_t *new_type)
{
  return EOPNOTSUPP;
}

error_t
S_file_set_size (struct sock_user *cred, off_t size)
{
  return EOPNOTSUPP;
}

error_t
S_file_set_translator (struct sock_user *cred,
		       int passive_flags,
		       int active_flags,
		       int killtrans_flags,
		       char *passive,
		       size_t passivelen,
		       fsys_t active)
{
  return EOPNOTSUPP;
}

error_t
S_file_statfs (struct sock_user *file, fsys_statfsbuf_t *statbuf)
{
  return EOPNOTSUPP;
}

error_t
S_file_sync (struct sock_user *cred, int wait, int omitmetadata)
{
  return EOPNOTSUPP;
}

error_t
S_file_syncfs (struct sock_user *cred, int wait, int dochildren)
{
  return EOPNOTSUPP;
}

error_t
S_file_utimes (struct sock_user *cred, time_value_t atime, time_value_t mtime)
{
  return EOPNOTSUPP;
}
