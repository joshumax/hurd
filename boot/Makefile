# Copyright (C) 1993, 1994, 1995 Free Software Foundation, Inc.
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

SRCS = boot.c tcattr.c sigvec.S syscall.S
OBJS = boot.o notifyServer.o execServer.o ourdeviceServer.o ioServer.o \
       io_replyUser.o device_replyUser.o sigvec.o syscall.o \
       termServer.o tcattr.o bootstrapServer.o
HURDLIBS = libthreads
target = boot
io-MIGSFLAGS=-DREPLY_PORTS
DISTFILES=frank1.ld frankemul.ld

include ../Makeconf

LDFLAGS += -Wl,-T -Wl,frank1.ld

#install: /usr/local/bin/boot
#
#/usr/local/bin/boot: boot
#	cp boot /usr/local/bin/boot

all: boot.a

ourdevice.defs: $(includedir)/device/device.defs
	sed -e '/out	device		: device_t/s/device_t/mach_port_send_t/' $< > $@

boot.1: boot frankemul.ld
	i386-gnuelf-ld -o boot.1 -T frankemul.ld boot
boot.a: boot.1 
	i386-gnuelf-objcopy -S --remove-section=.comment -O a.out-mach3 \
			    boot.1 boot.a
