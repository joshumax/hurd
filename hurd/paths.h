/* Standard Hurd pathnames.
   Copyright (C) 1992,94,95,97,2002 Free Software Foundation, Inc.

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

#ifndef	_HURD_PATHS_H
#define	_HURD_PATHS_H

/* Port rendezvous points are specified by symbols _SERVERS_FOO,
   the canonical pathname being /servers/foo.  */

#define	_SERVERS		"/servers/"
#define	_SERVERS_CRASH		_SERVERS "crash"
#define	_SERVERS_EXEC		_SERVERS "exec"
#define _SERVERS_STARTUP	_SERVERS "startup"
#define _SERVERS_PROC		_SERVERS "proc"
#define _SERVERS_PASSWORD	_SERVERS "password"
#define _SERVERS_DEFPAGER	_SERVERS "default-pager"

/* Directory containing naming points for socket servers.
   Entries are named by the string representing the domain number
   in simple decimal (e.g. "/servers/socket/23").  */
#define	_SERVERS_SOCKET		_SERVERS "socket"

/* Directory containing virtual filesystems for buses */
#define	_SERVERS_BUS		_SERVERS "bus"

/* Hurd servers are specified by symbols _HURD_FOO,
   the canonical pathname being /hurd/foo.  */

#define	_HURD		"/hurd/"
#define	_HURD_STARTUP	_HURD "startup"
#define _HURD_PROC	_HURD "proc"
#define _HURD_AUTH	_HURD "auth"

/* Standard translators for special node types.
   These pathnames are used by the C library.
   UFS and perhaps other filesystems short-circuit these translators.  */
#define	_HURD_SYMLINK	_HURD "symlink" /* S_IFLNK */
#define	_HURD_CHRDEV	_HURD "chrdev" /* S_IFCHR */
#define	_HURD_BLKDEV	_HURD "blkdev" /* S_IFBLK */
#define	_HURD_FIFO	_HURD "fifo" /* S_IFIFO */
#define	_HURD_IFSOCK	_HURD "ifsock" /* S_IFSOCK */

/* Symbolic names for all non-essential translators.  */
#define _HURD_MAGIC	_HURD "magic"
#define _HURD_MTAB	_HURD "mtab"

#endif	/* hurd/paths.h */
