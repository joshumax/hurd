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

dir := exec

include ../Makeconf

VPATH=.:../machine

CFLAGS := $(CFLAGS) -Dinline= -DA_OUT_H=\"./a.out.h\"

DIST_FILES = Makefile exec.c hostarch.c core.c gcore.c hashexec.c \
	     a.out.h a.out.gnu.h ChangeLog
OBJS = hostarch.o exec.o execServer.o fsysServer.o notifyServer.o

SRCS = hostarch.c exec.c
TAGSHDRS = a.out.h a.out.gnu.h

all: exec

exec: $(OBJS)
	$(link)

exec.o: exec_server.h fsys_S.h notify_S.h 

exec.o hostarch.o: a.out.h a.out.gnu.h

install:

clean: 
	rm -f *.o exec exec_server.h *_S.h *Server.c
relink:
	rm -f exec

exec_server.h execServer.c: $(includedir)/hurd/exec.defs $(includedir)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) -DSERVERPREFIX=S_ $(includedir)/hurd/exec.defs \
	| $(MIGCOM) -sheader exec_server.h -header /dev/null -user /dev/null


fsys_S.h fsysServer.c: $(includedir)/hurd/fsys.defs $(includedir)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) -DSERVERPREFIX=S_  $(includedir)/hurd/fsys.defs \
	| $(MIGCOM) -sheader fsys_S.h -header /dev/null -user /dev/null

notify_S.h notifyServer.c: $(includedir)/mach/notify.defs
	$(CPP) $(CPPFLAGS)  $(includedir)/mach/notify.defs \
	| $(MIGCOM) -sheader notify_S.h -header /dev/null -user /dev/null
