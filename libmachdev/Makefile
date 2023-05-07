# Copyright (C) 2009 Free Software Foundation, Inc.
# This file is part of the GNU Hurd.
#
# The GNU Hurd is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# The GNU Hurd is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with the GNU Hurd; see the file COPYING.  If not, write to
# the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

dir := libmachdev
makemode := library
libname = libmachdev

SRCS = ds_routines.c trivfs_server.c startup_notifyServer.c \
       deviceServer.c mach_i386Server.c

LCLHDRS = machdev.h machdev-device_emul.h machdev-dev_hdr.h mach_device.h trivfs_server.h
installhdrs = machdev.h machdev-device_emul.h machdev-dev_hdr.h
HURDLIBS = ports trivfs
LDLIBS += -lpthread -lmachuser
OBJS = $(SRCS:.c=.o) $(MIGSTUBS)
MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h
device-MIGSFLAGS=-DMACH_PAYLOAD_TO_PORT=ports_payload_get_name -DDEVICE_ENABLE_DEVICE_OPEN_NEW
mach_i386-MIGSFLAGS="-DMACH_PAYLOAD_TO_PORT=ports_payload_get_name" \
  "-DMACH_I386_IMPORTS=import \"$(srcdir)/../libports/ports.h\";"

include ../Makeconf
