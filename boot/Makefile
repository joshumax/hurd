# Copyright (C) 1993, 1994, 1995, 1996 Free Software Foundation, Inc.
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

SRCS = mach-crt0.c boot.c ux.c sigvec.S syscall.S boot_script.c
COMMON-OBJS = notifyServer.o ourdeviceServer.o \
       ioServer.o io_replyUser.o device_replyUser.o \
       termServer.o bootstrapServer.o boot_script.o
OBJS = boot.o $(COMMON-OBJS)
UX-OBJS = mach-crt0.o uxboot.o sigvec.o syscall.o ux.o $(COMMON-OBJS)
LCLHDRS = boot_script.h ux.h
target = boot
io-MIGSFLAGS=-DREPLY_PORTS
DIST_FILES=frank1.ld frankemul.ld

include ../Makeconf

#install: /usr/local/bin/uxboot
#
#/usr/local/bin/uxboot: uxboot
#	cp $< $@

all: boot uxboot

ourdevice.defs: $(includedir)/device/device.defs
	sed -e '/out	device		: device_t/s/device_t/mach_port_send_t/' $< > $@

boot: ../libthreads/libthreads.so

uxboot.o: boot.c
	$(COMPILE.c) -DUX $< -o $@

uxboot.0: $(UX-OBJS) ../libthreads/libthreads.a
	$(LINK.o) -o $@ -static -nostartfiles -Wl,-T -Wl,$(srcdir)/frank1.ld $^
uxboot.1: frankemul.ld uxboot.0
	$(LD) -o $@ -T $^
uxboot: uxboot.1
	-$(OBJCOPY) -S --remove-section=.comment -O a.out-mach3 $< $@
