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

#define FILE_INTRAN protid_t begin_using_protid_port (file_t)
#define FILE_DESTRUCTOR end_using_protid_port (protid_t)

#define IO_INTRAN protid_t begin_using_protid_port (io_t)
#define IO_DESTRUCTOR end_using_protid_port (protid_t)

#define SERVERPREFIX diskfs_S_

#define FILE_IMPORTS import "priv.h";
#define IO_IMPORTS import "priv.h";
#define FSYS_IMPORTS import "priv.h";
#define IFSOCK_IMPORTS import "priv.h";
