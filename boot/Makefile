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

OBJS = boot.o notifyServer.o execServer.o deviceServer.o boot_machdep.o

DIST_FILES = boot.c Makefile

boot: $(OBJS)
	$(link)

install: boot
	cp boot /usr/local/bin/boot

clean: 
	rm -f boot $(OBJS) *_S.h *Server.c

boot.o: notify_S.h exec_S.h device_S.h

notify_S.h notifyServer.c: $(headers)/mach/notify.defs
	$(CPP) $(CPPFLAGS) $< | \
	$(MIGCOM) -user /dev/null -header /dev/null \
	-sheader notify_S.h

exec_S.h execServer.c: $(headers)/hurd/exec.defs
	$(CPP) $(CPPFLAGS) $< -DSERVERPREFIX=S_ | \
	$(MIGCOM) -user /dev/null -header /dev/null \
	-sheader exec_S.h

device_S.h deviceServer.c: $(headers)/device/device.defs
	$(CPP) $(CPPFLAGS) $< | \
	$(MIGCOM) -user /dev/null -header /dev/null \
	-sheader device_S.h
