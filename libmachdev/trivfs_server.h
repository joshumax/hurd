/*
   Copyright (C) 2020 Free Software Foundation, Inc.

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

#ifndef _MACHDEV_TRIVFS_SERVER_H
#define _MACHDEV_TRIVFS_SERVER_H

#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd.h>

extern struct port_bucket *port_bucket;
extern struct port_class *trivfs_protid_class;
extern struct port_class *trivfs_cntl_class;
extern struct port_class *machdev_shutdown_notify_class;

#endif /* _MACHDEV_TRIVFS_SERVER_H */

