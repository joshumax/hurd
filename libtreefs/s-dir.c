/* File_t rpc stubs for directories; see <hurd/fs.defs> for more info

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   Note that since the user overrides the builtin routines via hook vectors
   instead of declaring his own stubs, it doesn't make a lot of sense to put
   these routines in separate files like diskfs.  This way should compile
   faster; with dynamic libraries it  won't matter in any case.

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
treefs_S_dir_notice_changes (struct treefs_protid *cred,
			     mach_port_t notify)
{
  if (cred == NULL)
    return EOPNOTSUPP;
  return treefs_s_dir_notice_changes (cred, notify);
}  

error_t
treefs_S_dir_link (struct treefs_protid *dir_cred,
		   struct treefs_protid *file_cred,
		   char *name)
{
  if (cred == NULL)
    return EOPNOTSUPP;
  return treefs_s_dir_link (dir_cred, file_cred, name);
}

error_t
treefs_S_dir_lookup (struct treefs_protid *cred,
		     char *path, int flags, mode_t mode,
		     enum retry_type *retry, char *retry_name,
		     file_t *result, mach_msg_type_name_t *result_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_dir_lookup (cred, path, flags, mode, retry, retry_name,
			      result, result_type);
}

error_t
treefs_S_dir_mkdir (struct treefs_protid *cred, char *name, mode_t mode)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_dir_mkdir (cred, name, mode);
}

error_t
treefs_S_dir_mkfile (struct treefs_protid *cred,
		     int flags, mode_t mode,
		     mach_port_t *newnode, mach_msg_type_name_t *newnode_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_dir_mkfile (cred, flags, mode, newnode, newnode_type);
}  

error_t
treefs_S_dir_readdir (struct treefs_protid *cred,
		      char **data, unsigned *datacnt,
		      int entry, int num_entries,
		      vm_size_t bufsiz, int *amt)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_dir_readdir (cred, data, datacnt,
			       entry, num_entries, bufsiz, amt);
}

error_t
treefs_S_dir_rename (struct treefs_protid *cred, char *name,
		     struct treefs_protid *to_cred, char *to_name)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_dir_rename (cred, name, to_cred, to_name);
}

error_t
treefs_S_dir_rmdir (struct treefs_protid *cred, char *name)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_dir_rmdir (cred, name);
}

error_t
treefs_S_dir_unlink (struct treefs_protid *cred, char *name)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_dir_unlink (cred, name);
}
