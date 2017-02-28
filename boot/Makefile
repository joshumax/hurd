# Copyright (C) 1993,94,95,96,97,2001,2012 Free Software Foundation, Inc.
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

dir := boot
makemode := utility

SRCS = boot.c boot_script.c userland-boot.c
COMMON-OBJS = notifyServer.o deviceServer.o \
       ioServer.o io_replyUser.o device_replyUser.o \
       termServer.o boot_script.o userland-boot.o
MIGSTUBS = machServer.o mach_hostServer.o gnumachServer.o task_notifyServer.o
OBJS = boot.o $(COMMON-OBJS) $(MIGSTUBS)
target = boot
MIGSFLAGS=-imacros $(srcdir)/mig-mutate.h -DHURD_DEFAULT_PAYLOAD_TO_PORT=1
io-MIGSFLAGS=-DREPLY_PORTS -DHURD_DEFAULT_PAYLOAD_TO_PORT=1
HURDLIBS = store shouldbeinlibc ihash
LDLIBS += -lpthread

include ../Makeconf
