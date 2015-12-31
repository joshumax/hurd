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

SRCS = mach-crt0.c boot.c ux.c sigvec.S syscall.S \
       boot_script.c userland-boot.c
COMMON-OBJS = notifyServer.o deviceServer.o \
       ioServer.o io_replyUser.o device_replyUser.o \
       termServer.o bootstrapServer.o boot_script.o userland-boot.o
OBJS = boot.o $(COMMON-OBJS)
UX-OBJS = mach-crt0.o uxboot.o sigvec.o syscall.o ux.o $(COMMON-OBJS)
target = boot
io-MIGSFLAGS=-DREPLY_PORTS
HURDLIBS = store shouldbeinlibc
LDLIBS += -lpthread

include ../Makeconf

#install: /usr/local/bin/uxboot
#
#/usr/local/bin/uxboot: uxboot
#	cp $< $@

MIGSFLAGS = -DHURD_DEFAULT_PAYLOAD_TO_PORT=1

all: boot # uxboot

uxboot.o: boot.c
	$(COMPILE.c) -DUX $< -o $@

uxboot.0: $(UX-OBJS)
	$(LINK.o) -o $@ -static -nostartfiles -Wl,-T -Wl,$(srcdir)/frank1.ld $^
uxboot.1: frankemul.ld uxboot.0
	$(LD) -o $@ -T $^
uxboot: uxboot.1
	-$(OBJCOPY) -S --remove-section=.comment -O a.out-mach3 $< $@
