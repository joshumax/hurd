/* Standard Hurd pathnames.
   Copyright (C) 1991 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 1, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#pragma once


/* Port rendezvous points are specified by symbols _SERVERS_FOO,
   the canonical pathname being /servers/foo.  */

#define	_SERVERS_CORE		"/servers/core"
#define	_SERVERS_EXEC		"/servers/exec"
#define _SERVERS_STARTUP	"/servers/startup"
#define _SERVERS_PROC		"/servers/proc"
#define _SERVERS_NEWTERM	"/servers/newterm"

/* Directory containing naming points for socket servers.
   Entries are named by the string representing the domain number
   in simple decimal (e.g. "/servers/socket/23").  */
#define	_SERVERS_SOCKET		"/servers/socket"

/* Hurd servers are specified by symbols _HURD_FOO,
   the canonical pathname being /libexec/foo.  */

/* Standard translators for special node types.
   These pathnames are used by the C library.
   UFS and perhaps other filesystems short-circuit these translators.  */
#define	_HURD_SYMLINK	"/libexec/symlink" /* S_IFLNK */
#define	_HURD_CHRDEV	"/libexec/chrdev" /* S_IFCHR */
#define	_HURD_BLKDEV	"/libexec/blkdev" /* S_IFBLK */
#define	_HURD_FIFO	"/libexec/fifo" /* S_IFIFO */
#define	_HURD_IFSOCK	"/libexec/ifsock" /* S_IFSOCK */
