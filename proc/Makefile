# Copyright (C) 1992, 1993, 1994 Free Software Foundation, Inc.
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

dir := proc

include ../Makeconf

PROCMIGOPTS="-DPROCESS_INTRAN=pstruct_t reqport_find (process_t)" \
	"-DPROCESS_IMPORTS=import \"proc.h\";" \
	"-DSERVERPREFIX=S_"

OBJS = proc_wait.o proc_hash.o proc_host.o \
	proc_info.o proc_main.o proc_mgt.o \
	proc_notify.o proc_pgrp.o proc_msg.o \
	processServer.o notifyServer.o process_replyUser.o msgUser.o \
	interruptServer.o

SRCS = proc_wait.c proc_hash.c proc_host.c proc_info.c proc_main.c proc_mgt.c \
	proc_notify.c proc_pgrp.c proc_coll.c proc_msg.c 

DIST_FILES = $(SRCS) proc.h Makefile

all: proc

proc: $(OBJS)
	$(link)

$(OBJS): proc.h proc_S.h
proc_notify.o: notify_S.h
proc_wait.o: process_reply.h msg.h interrupt_S.h

proc_S.h processServer.c: $(headers)/hurd/process.defs $(headers)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) $(PROCMIGOPTS) $(headers)/hurd/process.defs \
	| $(MIGCOM) -sheader proc_S.h -user /dev/null -header /dev/null

process_reply.h process_replyUser.c: $(headers)/hurd/process_reply.defs $(hurd/hurd_types.defs)
	$(CPP) $(CPPFLAGS) $(headers)/hurd/process_reply.defs \
	| $(MIGCOM) -server /dev/null

interrupt_S.h interruptServer.c: $(headers)/hurd/interrupt.defs
	$(CPP) $(CPPFLAGS) $(PROCMIGOPTS) $(headers)/hurd/interrupt.defs \
	| $(MIGCOM) -sheader interrupt_S.h -user /dev/null -header /dev/null

notify_S.h notifyServer.c:
	$(CPP) $(CPPFLAGS) $(headers)/mach/notify.defs \
	 | $(MIGCOM) -sheader notify_S.h -header /dev/null -user /dev/null

# The reason for -Dout= is to prevent errors for get_init_port,
# get_init_ports, get_init_int, get_init_ints, get_dtable, and get_fd.
# We don't use those, so we're safe in breaking them.
msg.h msgUser.c: $(headers)/hurd/msg.defs $(headers)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) -Droutine=simpleroutine -Dout= $(headers)/hurd/msg.defs \
	| $(MIGCOM) -prefix nowait_ -server /dev/null

clean:
	rm -f *.o *Server.c notify_S.h  proc_S.h proc process_reply.h \
		process_replyUser.c msg.h msgUser.c
