/* 
   Copyright (C) 1994,2001 Free Software Foundation

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

#define FILE_INTRAN protid_t diskfs_begin_using_protid_port (file_t)
#define FILE_INTRAN_PAYLOAD protid_t diskfs_begin_using_protid_payload
#define FILE_DESTRUCTOR diskfs_end_using_protid_port (protid_t)

#define IO_INTRAN protid_t diskfs_begin_using_protid_port (io_t)
#define IO_INTRAN_PAYLOAD protid_t diskfs_begin_using_protid_payload
#define IO_DESTRUCTOR diskfs_end_using_protid_port (protid_t)

#define FSYS_INTRAN control_t diskfs_begin_using_control_port (fsys_t)
#define FSYS_INTRAN_PAYLOAD control_t diskfs_begin_using_control_port_payload
#define FSYS_DESTRUCTOR diskfs_end_using_control_port (control_t)

#define FILE_IMPORTS import "libdiskfs/priv.h";
#define IO_IMPORTS import "libdiskfs/priv.h";
#define FSYS_IMPORTS import "libdiskfs/priv.h";
#define IFSOCK_IMPORTS import "libdiskfs/priv.h";

#define EXEC_STARTUP_INTRAN                             \
  bootinfo_t diskfs_begin_using_bootinfo_port (exec_startup_t)
#define EXEC_STARTUP_INTRAN_PAYLOAD                     \
  bootinfo_t diskfs_begin_using_bootinfo_payload
#define EXEC_STARTUP_DESTRUCTOR                         \
  diskfs_end_using_bootinfo (bootinfo_t)
#define EXEC_STARTUP_IMPORTS                            \
  import "libdiskfs/priv.h";
