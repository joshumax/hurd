/* Type translation for rtc operations

   Copyright (C) 2024 Free Software Foundation, Inc.

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

#define IO_INTRAN trivfs_protid_t trivfs_begin_using_protid (io_t)
#define IO_INTRAN_PAYLOAD trivfs_protid_t trivfs_begin_using_protid_payload
#define IO_DESTRUCTOR trivfs_end_using_protid (trivfs_protid_t)
#define PIOCTL_IMPORTS import "../libtrivfs/mig-decls.h";
