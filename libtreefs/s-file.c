/* File_t rpc stubs; see <hurd/fs.defs> for more info

   Copyright (C) 1995, 1997 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   Note that since the user overrides the builtin routines via hook vectors
   instead of declaring his own stubs, it doesn't make a lot of sense to put
   these routines in separate files (like diskfs).  This way should compile
   faster, with dynamic libraries it  won't matter in any case.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

error_t
treefs_S_file_check_access (struct treefs_protid *cred, int *type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_check_access (cred, type);
}

error_t
treefs_S_file_chauthor (struct treefs_protid *cred, uid_t author)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_chauthor (cred, author);
}

error_t
treefs_S_file_chflags (struct treefs_protid *cred, int flags)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_chflags (cred, flags);
}

error_t
treefs_S_file_notice_changes (struct treefs_protid *cred, mach_port_t notify)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_notice_changes (cred, notify);
}

error_t
treefs_S_file_chmod (struct treefs_protid *cred, mode_t mode)
{
  if (!cred)
    return EOPNOTSUPP;
  mode &= ~(S_IFMT | S_ISPARE | S_ITRANS);
  return treefs_s_file_chmod (cred, mode);
}

error_t
treefs_S_file_chown (struct treefs_protid *cred, uid_t uid, gid_t gid)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_chown (cred, uid, gid);
}

error_t 
treefs_S_file_exec (struct treefs_protid *cred,
		    task_t task, int flags,
		    char *argv, unsigned argv_len,
		    char *envp, unsigned envp_len,
		    mach_port_t *fds, unsigned fds_len,
		    mach_port_t *ports, unsigned ports_len,
		    int *ints, unsigned ints_len,
		    mach_port_t *dealloc, unsigned dealloc_len,
		    mach_port_t *destroy, unsigned destroy_len)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_exec (cred, task, flags, argv, argv_len, envp, envp_len,
			   fds, fds_len, ports, ports_len, ints, ints_len,
			   dealloc, dealloc_len, destroy, destroy_len);
}

error_t
treefs_S_file_get_translator (struct treefs_protid *cred,
			      char **trans, unsigned *trans_len)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_get_translator (cred, trans, trans_len);
}

error_t
treefs_S_file_get_translator_cntl (struct treefs_protid *cred,
				   mach_port_t *ctl,
				   mach_msg_type_name_t *ctl_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_get_translator_cntl (cred, ctl, ctl_type);
}

error_t
treefs_S_file_getcontrol (struct treefs_protid *cred,
			  mach_port_t *control,
			  mach_msg_type_name_t *control_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_getcontrol (cred, control, control_type);
}

error_t
treefs_S_file_getfh (struct treefs_protid *cred,
		     char **data, unsigned *data_len)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_getfh (cred, data, data_len);
}

error_t
treefs_S_file_getlinknode (struct treefs_protid *cred,
			   file_t *port, mach_msg_type_name_t *port_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_t (cred, port, port_type);
}

error_t
treefs_S_file_invoke_translator (struct treefs_protid *cred,
				 int flags,
				 retry_type *retry, char *retry_name,
				 mach_port_t *retry_port,
				 mach_msg_type_name_t *retry_port_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_invoke_translator (cred, flags, retry, retry_name,
					retry_port, retry_port_type);
}

error_t
treefs_S_file_lock_stat (struct treefs_protid *cred,
			 int *self_status, int *other_status)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_lock_stat (cred, self_status, other_status);
}

error_t
treefs_S_file_lock (struct treefs_protid *cred, int flags)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_lock (cred, flags);
}

error_t
treefs_S_file_pathconf (struct treefs_protid *cred, int name, int *value)
 {
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_pathconf (cred, name, value);
}

error_t
treefs_S_file_set_translator (struct treefs_protid *cred,
			      int passive_flags, int active_flags,
			      int killtrans_flags,
			      char *passive, unsigned passive_len,
			      fsys_t active)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_set_translator (cred, passive_flags, active_flags,
				     killtrans_flags, passive, passive_len,
				     active);
}

error_t
treefs_S_file_statfs (struct treefs_protid *cred, fsys_statfsbuf_t *statbuf)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_statfs (cred, statbuf);
}

error_t
treefs_S_file_sync (struct treefs_protid *cred, int wait)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_sync (cred, wait);
}

error_t
treefs_S_file_syncfs (struct treefs_protid *cred, int wait, int recurse)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_syncfs (cred, wait, recurse);
}

error_t
treefs_S_file_set_size (struct treefs_protid *cred, off_t size)
{
  if (!cred)
    return EOPNOTSUPP;
  else if (size < 0)
    return EINVAL;
  return treefs_s_file_set_size (cred, size);
}

error_t
treefs_S_file_utimes (struct treefs_protid *cred,
		      time_value_t atime, time_value_t mtime)
{
  if (!cred)
    return EOPNOTSUPP;

  struct timespec atim, mtim;

  if (atime.microseconds == -1)
    {
      atim.tv_sec = 0;
      atim.tv_nsec = UTIME_NOW;
    }
  else
    TIME_VALUE_TO_TIMESPEC (&atime, &atim);

  if (mtime.microseconds == -1)
    {
      mtim.tv_sec = 0;
      mtim.tv_nsec = UTIME_NOW;
    }
  else
    TIME_VALUE_TO_TIMESPEC (&mtime, &mtim);

  return treefs_s_file_utimens (cred, atim, mtim);
}

error_t
treefs_S_file_utimens (struct treefs_protid *cred,
		      struct timespec atime, struct timespec mtime)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_file_utimens (cred, atime, mtime);
}
