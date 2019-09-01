/* Return the file for a given handle (for nfs server support)

   Copyright (C) 1997,99,2001,02 Free Software Foundation, Inc.

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

#include "priv.h"
#include "fsys_S.h"
#include "fhandle.h"

/* Return in FILE & FILE_TYPE the file in FSYS corresponding to the NFS file
   handle HANDLE & HANDLE_LEN.  */
error_t
diskfs_S_fsys_getfile (struct diskfs_control *pt,
		       mach_port_t reply, mach_msg_type_name_t reply_type,
		       uid_t *uids, mach_msg_type_number_t nuids,
		       gid_t *gids, mach_msg_type_number_t ngids,
		       data_t handle, mach_msg_type_number_t handle_len,
		       mach_port_t *file, mach_msg_type_name_t *file_type)
{
  int flags;
  error_t err;
  struct node *node;
  const union diskfs_fhandle *f;
  struct protid *new_cred;
  struct peropen *new_po;
  struct iouser *user;

  if (!pt)
    return EOPNOTSUPP;

  if (handle_len != sizeof *f)
    {
      return EINVAL;
    }

  f = (const union diskfs_fhandle *) handle;

  err = diskfs_cached_lookup (f->data.cache_id, &node);
  if (err)
    {
      return err;
    }

  if (node->dn_stat.st_gen != f->data.gen)
    {
      diskfs_nput (node);
      return ESTALE;
    }

  err = iohelp_create_complex_iouser (&user, uids, nuids, gids, ngids);
  if (err)
    {
      diskfs_nput (node);
      return err;
    }

  flags = 0;
  if (! fshelp_access (&node->dn_stat, S_IREAD, user))
    flags |= O_READ;
  if (! fshelp_access (&node->dn_stat, S_IEXEC, user))
    flags |= O_EXEC;
  if (! fshelp_access (&node->dn_stat, S_IWRITE, user)
      && ! S_ISDIR (node->dn_stat.st_mode)
      && ! diskfs_check_readonly ())
    flags |= O_WRITE;

  err = diskfs_make_peropen (node, flags, 0, &new_po);
  if (! err)
    {
      err = diskfs_create_protid (new_po, user, &new_cred);
      if (err)
	diskfs_release_peropen (new_po);
    }

  iohelp_free_iouser (user);

  diskfs_nput (node);

  if (! err)
    {
      *file = ports_get_right (new_cred);
      *file_type = MACH_MSG_TYPE_MAKE_SEND;
    }

  return err;
}
