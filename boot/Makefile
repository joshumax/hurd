# Copyright (C) 1993, 1994 Free Software Foundation, Inc.
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

include ../Makeconf

VPATH=.:../machine

OBJS = boot.o notifyServer.o execServer.o deviceServer.o ioServer.o \
	io_replUser.o device_replyUser.o sigvec.o

DIST_FILES = boot.c Makefile mungio io_repl.defs ChangeLog

all: boot

boot: $(OBJS)
	$(link)

install: boot
	cp boot /usr/local/bin/boot

clean: 
	rm -f boot $(OBJS) *_S.h *Server.c
relink:
	rm -f boot

boot.o: notify_S.h exec_S.h device_S.h io_S.h

notify_S.h notifyServer.c: $(headers)/mach/notify.defs
	$(CPP) $(CPPFLAGS) $< | \
	$(MIGCOM) -user /dev/null -header /dev/null \
	-sheader notify_S.h

exec_S.h execServer.c: $(headers)/hurd/exec.defs
	$(CPP) $(CPPFLAGS) $< -DSERVERPREFIX=S_ | \
	$(MIGCOM) -user /dev/null -header /dev/null \
	-sheader exec_S.h

device.defs: $(headers)/device/device.defs
	sed -e '/out	device		: device_t/s/device_t/mach_port_send_t/' $< > $@

device_S.h deviceServer.c: device.defs
	$(CPP) $(CPPFLAGS) $< | \
	$(MIGCOM) -user /dev/null -header /dev/null \
	-sheader device_S.h

device_replyUser.c: $(headers)/device/device_reply.defs
	$(CPP) $(CPPFLAGS) $< | \
	$(MIGCOM) -server /dev/null

io.defs: $(headers)/hurd/io.defs mungio
	sed -f mungio $< > $@
io_S.h ioServer.c: io.defs
	$(CPP) $(CPPFLAGS) $< | \
	$(MIGCOM) -prefix S_ -user /dev/null -header /dev/null -sheader io_S.h

io_repl.h io_replUser.c: io_repl.defs
	$(CPP) $(CPPFLAGS) $< | \
	$(MIGCOM) -server /dev/null
