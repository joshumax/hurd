/* 
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */


struct proctable nfstable = 
{
  NFSPROC_NULL,			/* first proc */
  NFSPROC_STATFS,		/* last proc */
  { op_null, 0, 0},
  { op_getattr, 0, 1},
  { op_setattr, 0, 1},
  { 0, 0, 0 },			/* deprecated NFSPROC_ROOT */
  { op_lookup, 0, 1},
  { op_readlink, 0, 1},
  { op_read, count_read_buffersize, 1},
  { 0, 0, 0 },			/* nonexistent NFSPROC_WRITECACHE */
  { op_write, 0, 1},
  { op_create, 0, 1},
  { op_remove, 0, 1},
  { op_rename, 0, 1},
  { op_link, 0, 1},
  { op_symlink, 0, 1},
  { op_mkdir, 0, 1},
  { op_rmdir, 0, 1},
  { op_readdir, count_readdir_buffersize, 1},
  { op_statfs, 0, 1},
};

   
struct proctable mounttable =
{
  MOUNTPROC_NULL,		/* first proc */
  MOUNTPROC_EXPORT,		/* last proc */
  { op_null, 0, 0},
  { op_mnt, 0, 0},
  { 0, 0, 0},			/* MOUNTPROC_DUMP */
  { 0, 0, 0},			/* MOUNTPROC_UMNT */
  { 0, 0, 0},			/* MOUNTPROC_UMNTALL */
  { 0, 0, 0},			/* MOUNTPROC_EXPORT */
};
