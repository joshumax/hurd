/* io_t rpc stubs; see <hurd/io.defs> for more info

   Copyright (C) 1995 Free Software Foundation, Inc.

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
treefs_S_io_get_icky_async_id (struct treefs_protid *cred,
			       mach_port_t *id, mach_msg_type_name_t *id_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_get_icky_async_id (cred, id, id_type);
}

error_t
treefs_S_io_async (struct treefs_protid *cred,
		   mach_port_t notify,
		   mach_port_t *id, mach_msg_type_name_t *id_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_async (cred, notify, id, id_type);
}

error_t
treefs_S_io_duplicate (struct treefs_protid *cred,
		       mach_port_t *port,
		       mach_msg_type_name_t *port_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_duplicate (cred, port, port_type);
}

error_t
treefs_S_io_get_conch (struct treefs_protid *cred)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_get_conch (cred);
}

error_t
treefs_S_io_interrupt (struct treefs_protid *cred)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_interrupt (cred);
}

error_t
treefs_S_io_map_cntl (struct treefs_protid *cred,
		      memory_object_t *ctlobj,
		      mach_msg_type_name_t *ctlobj_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_map_cntl (cred, ctlobj, ctlobj_type);
}

error_t
treefs_S_io_map (struct treefs_protid *cred,
		 memory_object_t *rdobj, mach_msg_type_name_t *rd_type,
		 memory_object_t *wrobj, mach_msg_type_name_t *wr_type)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_map (cred, rdobj, rd_type, wrobj, wr_type);
}

error_t
treefs_S_io_get_openmodes (struct treefs_protid *cred, int *bits)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_get_openmodes (cred, bits);
}

error_t
treefs_S_io_clear_some_openmodes (struct treefs_protid *cred, int bits)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_clear_some_openmodes (cred, bits);
}

error_t
treefs_S_io_set_some_openmodes (struct treefs_protid *cred, int bits)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_set_some_openmodes (cred, bits);
}

error_t
treefs_S_io_set_all_openmodes (struct treefs_protid *cred, int bits)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_set_all_openmodes (cred, bits);
}

error_t
treefs_S_io_get_owner (struct treefs_protid *cred, pid_t *owner)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_get_owner (cred, owner);
}

error_t
treefs_S_io_mod_owner (struct treefs_protid *cred, pid_t owner)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_mod_owner (cred, owner);
}

error_t
treefs_S_io_prenotify (struct treefs_protid *cred,
		       vm_offset_t start, vm_offset_t end)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_prenotify (cred, start, end);
}

error_t
treefs_S_io_read (struct treefs_protid *cred,
		  char **data,
		  mach_msg_type_number_t *data_len,
		  off_t offset,
		  mach_msg_type_number_t max)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_read (cred, data, data_len, offset, max);
}

error_t
treefs_S_io_readable (struct treefs_protid *cred,
		      mach_msg_type_number_t *amount)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_readable (cred, amount);
}

error_t
treefs_S_io_reauthenticate (struct treefs_protid *cred, mach_port_t rend_port)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_reauthenticate (cred, rend_port);
}

error_t
treefs_S_io_release_conch (struct treefs_protid *cred)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_release_conch (cred);
}

error_t
treefs_S_io_restrict_auth (struct treefs_protid *cred,
			   mach_port_t *newport,
			   mach_msg_type_name_t *newport_type,
			   uid_t *uids, unsigned nuids,
			   gid_t *gids, unsigned ngids)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_restrict_auth (cred, newport, newport_type,
				    uids, nuids, gids, ngids);
}

error_t
treefs_S_io_seek (struct treefs_protid *cred,
		  off_t offset, int whence, off_t *new_offset)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_seek (cred, offset, whence, new_offset);
}

error_t
treefs_S_io_select (struct treefs_protid *cred, int *type, int *tag)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_select (cred, type, tag);
}

error_t
treefs_S_io_sigio (struct treefs_protid *cred)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_sigio (cred);
}

error_t
treefs_S_io_stat (struct treefs_protid *cred, io_statbuf_t *statbuf)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_statbuf_t (cred, statbuf);
}

error_t
treefs_S_io_readsleep (struct treefs_protid *cred)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_readsleep (cred);
}

error_t
treefs_S_io_eofnotify (struct treefs_protid *cred)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_eofnotify (cred);
}

error_t
treefs_S_io_postnotify (struct treefs_protid *cred,
			vm_offset_t start, vm_offset_t end)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_postnotify (cred, start, end);
}

error_t
treefs_S_io_readnotify (struct treefs_protid *cred)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_readnotify (cred);
}     

error_t
treefs_S_io_server_version (struct treefs_protid *cred,
			    char *server_name,
			    int *major, int *minor, int *edit)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_server_version (cred, server_version, major, minor, edit);
}

error_t
treefs_S_io_write (struct treefs_protid *cred,
		   char *data, mach_msg_type_number_t data_len,
		   off_t offset, mach_msg_type_number_t *amount)
{
  if (!cred)
    return EOPNOTSUPP;
  return treefs_s_io_write (cred, data, data_len, offset, amount);
}
