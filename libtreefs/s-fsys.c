/* fsys_t rpc stubs; see <hurd/fsys.defs> for more info

   Copyright (C) 1995 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "treefs.h"

#define CALL_FSYS_HOOK(hook, fsys_port, args...)                              \
{                                                                             \
  error_t _err;                                                               \
  struct treefs_fsys *_fsys = (struct treefs_fsys *)			      \
    ports_lookup_port (0, fsys_port, treefs_fsys_port_class);		      \
  if (!_fsys)                                                                 \
    return EOPNOTSUPP;                                                        \
  err = hook(_fsys , ##args);                                                 \
  ports_port_deref (&_fsys->pi);                                              \
  return _err;                                                                \
}

error_t
treefs_S_fsys_getroot (fsys_t fsys_port, mach_port_t dotdot,
		       uid_t *uids, unsigned nuids,
		       gid_t *gids, unsigned ngids,
		       int flags, retry_type *retry, char *retry_name,
		       file_t *result, mach_msg_type_name_t *result_type)
{
  CALL_FSYS_HOOK(treefs_s_fsys_getroot, fsys_port, dotdot, uids, nuids, gids,
		 ngids, flags, retry, retry_name, result, result_type); 
}

error_t
treefs_S_fsys_set_options (fsys_t fsys_port,
			   char *data, unsigned len, int recurse)
{
  CALL_FSYS_HOOK(treefs_s_fsys_set_options, fsys_port, data, len, recurse);
}

error_t
treefs_S_fsys_goaway (fsys_t fsys_port, int flags)
{
  CALL_FSYS_HOOK(treefs_s_fsys_goaway, fsys_port, flags);
}

error_t
treefs_S_fsys_getfile (mach_port_t fsys_port,
		       uid_t *gen_uids, unsigned ngen_uids,
		       gid_t *gen_gids, unsigned ngen_gids,
		       char *handle, unsigned handle_len,
		       mach_port_t *file, mach_msg_type_name_t *file_type)
{
  CALL_FSYS_HOOK(treefs_s_fsys_getfile, fsys_port, gen_uids, ngen_uids,
		 gen_gids, ngen_gids, handle, handle_len, file, file_type); 
}

error_t
treefs_S_fsys_syncfs (fsys_t fsys_port, int wait, int recurse)
{
  CALL_FSYS_HOOK(treefs_s_fsys_syncfs, fsys_port, wait, recurse);
}
