# 
#   Copyright (C) 1994 Free Software Foundation
#
#   This program is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

dir := init.trim

include ../Makeconf

DIST_FILES = Makefile init.c

OBJS = init.o startupServer.o startup_replyUser.o

all: init

init: $(OBJS)
	$(link)

init.o: startup_S.h startup_reply.h

install: init $(hurdroot)
	cp init $(hurdroot)/hurd/init


startup_S.h startupServer.c: $(headers)/hurd/startup.defs $(headers)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) $(headers)/hurd/startup.defs \
	| $(MIGCOM) -prefix S_ -sheader startup_S.h -header /dev/null -user /dev/null

startup_reply.h startup_replyUser.c: $(headers)/hurd/startup_reply.defs $(headers)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) $(headers)/hurd/startup_reply.defs \
	| $(MIGCOM) -server /dev/null

clean:
	rm -f init *.o
