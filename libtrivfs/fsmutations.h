/* 
   Copyright (C) 1994 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* Only CPP macro definitions should go in this file. */

#define REPLY_PORTS

#define FILE_INTRAN trivfs_protid_t _trivfs_begin_using_protid (file_t)
#define FILE_DESTRUCTOR _trivfs_end_using_protid (trivfs_protid_t)

#define IO_INTRAN trivfs_protid_t _trivfs_begin_using_protid (io_t)
#define IO_DESTRUCTOR _trivfs_end_using_protid (trivfs_protid_t)

#define FSYS_INTRAN trivfs_control_t _trivfs_begin_using_control (fsys_t)
#define FSYS_DESTRUCTOR _trivfs_end_using_control (trivfs_control_t)

#define FILE_IMPORTS import "priv.h";
#define IO_IMPORTS import "priv.h";
#define FSYS_IMPORTS import "priv.h";
