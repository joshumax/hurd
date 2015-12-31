/*
   Copyright (C) 2014 Free Software Foundation, Inc.
   Written by Justus Winter.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

/* Only CPP macro definitions should go in this file. */

#define IO_INTRAN trivfs_protid_t trivfs_begin_using_protid (io_t)
#define IO_INTRAN_PAYLOAD trivfs_protid_t trivfs_begin_using_protid_payload
#define IO_DESTRUCTOR trivfs_end_using_protid (trivfs_protid_t)
#define IO_OUTTRAN io_t trivfs_convert_to_port (trivfs_protid_t)

#define CTTY_INTRAN					\
  port_info_t begin_using_ctty_port (mach_port_t)
#define CTTY_INTRAN_PAYLOAD				\
  port_info_t begin_using_ctty_payload
#define CTTY_DESTRUCTOR					\
  end_using_ctty (port_info_t)

#define TIOCTL_IMPORTS import "../libtrivfs/mig-decls.h";
#define TERM_IMPORTS					\
  import "../libtrivfs/mig-decls.h";			\
  import "mig-decls.h";
