/* RPC server hooks in libtreefs (also see "treefs-hooks.h")

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

#ifndef __TREEFS_S_HOOKS_H__
#define __TREEFS_S_HOOKS_H__

#include "treefs-hooks.h"

/* Shorthand for declaring the various hook types (each hook has an
   associated type so that a user can type-check his hook routine).  */
#define DHH(name_sym, ret_type, argtypes...) \
  typedef ret_type treefs_##name_sym##_t (struct treefs_handle * , ##argtypes);
#define DFH(name_sym, ret_type, argtypes...) \
  typedef ret_type treefs_##name_sym##_t (struct treefs_fsys * , ##argtypes);

/* ---------------------------------------------------------------- */
/* Hooks for file RPCs.  See <hurd/fs.defs> for more info.  */

DHH(s_file_exec, error_t,
    task_t, int, char *, unsigned, char *, unsigned, mach_port_t *, unsigned,
    mach_port_t *, unsigned, int *, unsigned,
    mach_port_t *, unsigned, mach_port_t *, unsigned)
#define treefs_s_file_exec(h, args...) \
  _TREEFS_CHH(h, S_FILE_EXEC, s_file_exec , ##args)
DHH(s_file_chown, error_t, uid_t, gid_t)
#define treefs_s_file_chown(h, args...)				      \
  _TREEFS_CHH(h, S_FILE_CHOWN, s_file_chown , ##args)
DHH(s_file_chauthor, error_t, uid_t)
#define treefs_s_file_chauthor(h, args...)				      \
  _TREEFS_CHH(h, S_FILE_CHAUTHOR, s_file_chauthor , ##args)
DHH(s_file_chmod, error_t, mode_t)
#define treefs_s_file_chmod(h, args...)				      \
  _TREEFS_CHH(h, S_FILE_CHMOD, s_file_chmod , ##args)
DHH(s_file_chflags, error_t, int)
#define treefs_s_file_chflags(h, args...)				      \
  _TREEFS_CHH(h, S_FILE_CHFLAGS, s_file_chflags , ##args)
DHH(s_file_utimens, error_t, struct timespec, struct timespec)
#define treefs_s_file_utimens(h, args...)				      \
  _TREEFS_CHH(h, S_FILE_UTIMENS, s_file_utimens , ##args)
DHH(s_file_truncate, error_t, off_t)
#define treefs_s_file_truncate(h, args...)				      \
  _TREEFS_CHH(h, S_FILE_TRUNCATE, s_file_truncate , ##args)
DHH(s_file_lock, error_t, struct treefs_handle *, int)
#define treefs_s_file_lock(h, args...)				      \
  _TREEFS_CHH(h, S_FILE_LOCK, s_file_lock , ##args)
DHH(s_file_lock_stat, error_t, int *, int *)
#define treefs_s_file_lock_stat(h, args...)				      \
  _TREEFS_CHH(h, S_FILE_LOCK_STAT, s_file_lock_stat , ##args)
DHH(s_file_notice_changes, error_t, mach_port_t)
#define treefs_s_file_notice_changes(h, args...)			      \
  _TREEFS_CHH(h, S_FILE_NOTICE, s_file_notice_changes , ##args)
DHH(s_file_sync, error_t, int)
#define treefs_s_file_sync(h, args...)				      \
  _TREEFS_CHH(h, S_FILE_SYNC, s_file_sync , ##args)
DHH(s_file_getlinknode, error_t, file_t *, mach_msg_type_name_t *)
#define treefs_s_file_getlinknode(h, args...)			      \
  _TREEFS_CHH(h, S_FILE_GET_LINK_NODE, s_file_getlinknode , ##args)

/* ---------------------------------------------------------------- */
/* Hooks for IO rpcs.  See <hurd/io.defs> for more info.  */

DHH(s_io_write, error_t, char *, unsigned, off_t, int *)
#define treefs_s_io_write(h, args...)				      \
  _TREEFS_CHH(h, S_IO_WRITE, s_io_write , ##args)
DHH(s_io_read, error_t, char **, unsigned *, off_t, int)
#define treefs_s_io_read(h, args...)					      \
  _TREEFS_CHH(h, S_IO_READ, s_io_read , ##args)
DHH(s_io_seek, error_t, off_t, int, off_t *)
#define treefs_s_io_seek(h, args...)					      \
  _TREEFS_CHH(h, S_IO_SEEK, s_io_seek , ##args)
DHH(s_io_readable, error_t, unsigned *)
#define treefs_s_io_readable(h, args...)				      \
  _TREEFS_CHH(h, S_IO_READABLE, s_io_readable , ##args)
DHH(s_io_set_all_openmodes, error_t, int)
#define treefs_s_io_set_all_openmodes(h, args...)			      \
  _TREEFS_CHH(h, S_IO_SET_ALL_OPENMODES, s_io_set_all_openmodes , ##args)
DHH(s_io_get_openmodes, error_t, int *)
#define treefs_s_io_get_openmodes(h, args...)			      \
  _TREEFS_CHH(h, S_IO_GET_OPENMODES, s_io_get_openmodes , ##args)
DHH(s_io_set_some_openmodes, error_t, int)
#define treefs_s_io_set_some_openmodes(h, args...)			      \
  _TREEFS_CHH(h, S_IO_SET_SOME_OPENMODES, s_io_set_some_openmodes , ##args)
DHH(s_io_clear_some_openmodes, error_t, int)
#define treefs_s_io_clear_some_openmodes(h, args...)			      \
  _TREEFS_CHH(h, S_IO_CLEAR_SOME_OPENMODES, s_io_clear_some_openmodes , ##args)
DHH(s_io_async, error_t, mach_port_t, mach_port_t *, mach_msg_type_name_t *)
#define treefs_s_io_async(h, args...)				      \
  _TREEFS_CHH(h, S_IO_ASYNC, s_io_async , ##args)
DHH(s_io_mod_owner, error_t, pid_t)
#define treefs_s_io_mod_owner(h, args...)				      \
  _TREEFS_CHH(h, S_IO_MOD_OWNER, s_io_mod_owner , ##args)
DHH(s_io_get_owner, error_t, pid_t *)
#define treefs_s_io_get_owner(h, args...)				      \
  _TREEFS_CHH(h, S_IO_GET_OWNER, s_io_get_owner , ##args)
DHH(s_io_get_icky_async_id, error_t, mach_port_t *, mach_msg_type_name_t *)
#define treefs_s_io_get_icky_async_id(h, args...)			      \
  _TREEFS_CHH(h, S_IO_GET_ICKY_ASYNC_ID, s_io_get_icky_async_id , ##args)
DHH(s_io_select, error_t, int *, int *)
#define treefs_s_io_select(h, args...)				      \
  _TREEFS_CHH(h, S_IO_SELECT, s_io_select , ##args)
DHH(s_io_stat, error_t, io_statbuf_t *)
#define treefs_s_io_stat(h, args...)					      \
  _TREEFS_CHH(h, S_IO_STAT, s_io_stat , ##args)
DHH(s_io_reauthenticate, error_t, mach_port_t *, mach_msg_type_name_t *)
#define treefs_s_io_reauthenticate(h, args...)			      \
  _TREEFS_CHH(h, S_IO_REAUTHENTICATE, s_io_reauthenticate , ##args)
DHH(s_io_restrict_auth, error_t,
    mach_port_t *, mach_msg_type_name_t *, uid_t *, int, gid_t *, int);
#define treefs_s_io_restrict_auth(h, args...)			      \
  _TREEFS_CHH(h, S_IO_RESTRICT_AUTH, s_io_restrict_auth , ##args)
DHH(s_io_duplicate, error_t, mach_port_t *, mach_msg_type_name_t *)
#define treefs_s_io_duplicate(h, args...)				      \
  _TREEFS_CHH(h, S_IO_DUPLICATE, s_io_duplicate , ##args)
DHH(s_io_server_version, error_t, char *, int *, int *, int *)
#define treefs_s_io_server_version(h, args...)			      \
  _TREEFS_CHH(h, S_IO_SERVER_VERSION, s_io_server_version , ##args)
DHH(s_io_map, error_t, mach_port_t *, mach_msg_type_name_t *, mach_port_t *, mach_msg_type_name_t *)
#define treefs_s_io_map(h, args...)					      \
  _TREEFS_CHH(h, S_IO_MAP, s_io_map , ##args)
DHH(s_io_map_cntl, error_t, mach_port_t *, mach_msg_type_name_t *)
#define treefs_s_io_map_cntl(h, args...)				      \
  _TREEFS_CHH(h, S_IO_MAP_CNTL, s_io_map_cntl , ##args)
DHH(s_io_release_conch, error_t, struct treefs_handle *)
#define treefs_s_io_release_conch(h, args...)			      \
 _TREEFS_CHH(h, S_IO_RELEASE_CONCH, s_io_release_conch, ##args)
DHH(s_io_eofnotify, error_t);
#define treefs_s_io_eofnotify(h, args...)				      \
  _TREEFS_CHH(h, S_IO_EOFNOTIFY, s_io_eofnotify , ##args)
DHH(s_io_prenotify, error_t, vm_offset_t, vm_offset_t);
#define treefs_s_io_prenotify(h, args...)				      \
  _TREEFS_CHH(h, S_IO_PRENOTIFY, s_io_prenotify , ##args)
DHH(s_io_postnotify, error_t, vm_offset_t, vm_offset_t);
#define treefs_s_io_postnotify(h, args...)				      \
  _TREEFS_CHH(h, S_IO_POSTNOTIFY, s_io_postnotify , ##args)
DHH(s_io_readnotify, error_t);
#define treefs_s_io_readnotify(h, args...)				      \
  _TREEFS_CHH(h, S_IO_READNOTIFY, s_io_readnotify , ##args)
DHH(s_io_readsleep, error_t);
#define treefs_s_io_readsleep(h, args...)				      \
  _TREEFS_CHH(h, S_IO_READSLEEP, s_io_readsleep , ##args)
DHH(s_io_sigio, error_t);
#define treefs_s_io_sigio(h, args...)				      \
  _TREEFS_CHH(h, S_IO_SIGIO, s_io_sigio , ##args)

/* ---------------------------------------------------------------- */
/* Hooks for directory RPCs.  See <hurd/fs.defs> for more info.  */

DHH(s_dir_lookup, error_t,						      
    char *, int, mode_t, enum retry_type *, char *,
    file_t *, mach_msg_type_name_t *);
#define treefs_s_dir_lookup(h, args...)				      \
  _TREEFS_CHH(h, S_DIR_LOOKUP, s_dir_lookup , ##args)
DHH(s_dir_readdir, error_t, char **, unsigned, int, int, vm_size_t, int *);
#define treefs_s_dir_readdir(h, args...)				      \
  _TREEFS_CHH(h, S_DIR_READDIR, s_dir_readdir , ##args)
DHH(s_dir_mkdir, error_t, char *, mode_t);
#define treefs_s_dir_mkdir(h, args...)				      \
  _TREEFS_CHH(h, S_DIR_MKDIR, s_dir_mkdir , ##args)
DHH(s_dir_rmdir, error_t, char *);
#define treefs_s_dir_rmdir(h, args...)				      \
  _TREEFS_CHH(h, S_DIR_RMDIR, s_dir_rmdir , ##args)
DHH(s_dir_unlink, error_t, char *);
#define treefs_s_dir_unlink(h, args...)				      \
  _TREEFS_CHH(h, S_DIR_UNLINK, s_dir_unlink , ##args)
DHH(s_dir_link, error_t, file_t, char *);
#define treefs_s_dir_link(h, args...)				      \
  _TREEFS_CHH(h, S_DIR_LINK, s_dir_link , ##args)
DHH(s_dir_rename, error_t, char *, file_t, char *);
#define treefs_s_dir_rename(h, args...)				      \
  _TREEFS_CHH(h, S_DIR_RENAME, s_dir_rename , ##args)
DHH(s_dir_mkfile, error_t, int, mode_t, mach_port_t *, mach_msg_type_name_t *);
#define treefs_s_dir_mkfile(h, args...)				      \
  _TREEFS_CHH(h, S_DIR_MKFILE, s_dir_mkfile , ##args)
DHH(s_dir_notice_changes, error_t, mach_port_t *, mach_msg_type_name_t *);
#define treefs_s_dir_notice_changes(h, args...)			      \
  _TREEFS_CHH(h, S_DIR_NOTICE_CHANGES, s_dir_notice_changes , ##args)

/* ---------------------------------------------------------------- */
/* fsys RPCs (called on the filesystem itself) */

DFH(s_fsys_getroot, error_t,
    mach_port_t, uid_t *, unsigned, gid_t *, unsigned, int,
    retry_type *, char *, file_t *, mach_msg_type_name_t *)
#define treefs_s_fsys_getroot(fsys, args...) \
  _TREEFS_CFH(fsys, S_FSYS_GETROOT, s_fsys_getroot , ##args)
DFH(s_fsys_set_options, error_t, char *, unsigned, int)
#define treefs_s_fsys_set_options(fsys, args...) \
  _TREEFS_CFH(fsys, S_FSYS_SET_OPTIONS, s_fsys_set_options , ##args)
DFH(s_fsys_goaway, error_t, int)
#define treefs_s_fsys_goaway(fsys, args...) \
  _TREEFS_CFH(fsys, S_FSYS_GOAWAY, s_fsys_goaway , ##args)
DFH(s_fsys_getfile, error_t,
    uid_t *, unsigned, gid_t *, unsigned, char *, unsigned,
    mach_port_t *, mach_msg_type_name_t *)
#define treefs_s_fsys_getfile(fsys, args...) \
  _TREEFS_CFH(fsys, S_FSYS_GETFILE, s_fsys_getfile , ##args)
DFH(s_fsys_syncfs, error_t, int, int)
#define treefs_s_fsys_syncfs(fsys, args...) \
  _TREEFS_CFH(fsys, S_FSYS_SYNCFS, s_fsys_syncfs , ##args)

/* Turn off our shorthand notation.  */
#undef DHH

/* ---------------------------------------------------------------- */
/* Default routines for some hooks (each is the default value for the hook
   with the same name minus the leading underscore).  When you add something
   here, you should also add it to the initialize code in defhooks.c.  */

treefs_s_dir_lookup_t _treefs_s_dir_lookup;
treefs_s_fsys_getroot_t _treefs_s_fsys_getroot;

#endif /* __TREEFS_S_HOOKS_H__ */
