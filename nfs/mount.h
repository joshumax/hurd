/* Manifest constants describing the mount protocol
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

/* These constants define the RPC mount protocol; see RFC 1094. */

#ifndef NFS_MOUNT_H
#define NFS_MOUNT_H

#define MOUNTPROG 100005
#define MOUNTVERS 1

/* Obnoxious arbitrary limits */
#define MOUNT_MNTPATHLEN 1024
#define MOUNT_MNTNAMLEN 255

#define MOUNTPROC_NULL 0
#define MOUNTPROC_MNT 1
#define MOUNTPROC_DUMP 2
#define MOUNTPROC_UMNT 3
#define MOUNTPROC_UMNTALL 4
#define MOUNTPROC_EXPORT 5

#endif /* NFS_MOUNT_H */
