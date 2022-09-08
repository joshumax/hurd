/*
   Copyright (C) 2010 Free Software Foundation, Inc.
   Written by Zheng Da.

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

/* This file declares interfaces used by driver translators.  */

#ifndef __MACHDEV_H__
#define __MACHDEV_H__

#include <mach.h>
#include "machdev-device_emul.h"
#include "machdev-dev_hdr.h"

extern struct port_bucket *machdev_device_bucket;

void machdev_register (struct machdev_device_emulation_ops *ops);

void machdev_device_init(void);
void machdev_device_sync(void);
void * machdev_server(void *);
error_t machdev_create_device_port (size_t size, void *result);
int machdev_trivfs_init(int argc, char **argv, mach_port_t bootstrap_resume_task, const char *name, const char *path, mach_port_t *bootstrap);
int machdev_demuxer(mach_msg_header_t *inp, mach_msg_header_t *outp);
void machdev_trivfs_server_startup(mach_port_t bootstrap);
void * machdev_trivfs_server_loop(void *);
boolean_t machdev_is_master_device (mach_port_t port);

#endif
