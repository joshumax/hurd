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

OBJS = wait.o hash.o host.o info.o main.o mgt.o	notify.o pgrp.o msg.o \
	primes.o \
	processServer.o notifyServer.o process_replyUser.o msgUser.o \
	interruptServer.o proc_excUser.o proc_excServer.o proc_excreplUser.o \
	proc_excreplServer.o

SRCS = wait.c hash.c host.c info.c main.c mgt.c	notify.c pgrp.c msg.c primes.c

DIST_FILES = $(SRCS) proc.h Makefile

all: proc

proc: $(OBJS)
	$(link)

$(OBJS): proc.h proc_S.h
notify.o: notify_S.h
wait.o: process_reply.h msg.h interrupt_S.h
mgt.o: proc_exc_S.h proc_exc.h proc_excrepl.h proc_excrepl_S.h

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

proc_exc_S.h proc_excUser.c proc_excServer.c proc_exc.h: proc_exc.defs
	$(CPP) $(CPPFLAGS) proc_exc.defs \
	| $(MIGCOM) -sheader proc_exc_S.h

proc_excrepl_S.h proc_excreplUser.c proc_excreplServer.c proc_excrepl.h: proc_excrepl.defs
	$(CPP) $(CPPFLAGS) proc_excrepl.defs \
	| $(MIGCOM) -sheader proc_excrepl_S.h


# The reason for -Dout= is to prevent errors for get_init_port,
# get_init_ports, get_init_int, get_init_ints, get_dtable, and get_fd.
# We don't use those, so we're safe in breaking them.
msg.h msgUser.c: $(headers)/hurd/msg.defs $(headers)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) -Droutine=simpleroutine -Dout= $(headers)/hurd/msg.defs \
	| $(MIGCOM) -prefix nowait_ -server /dev/null

clean:
	rm -f *.o *Server.c notify_S.h  proc_S.h proc process_reply.h \
		process_replyUser.c msg.h msgUser.c
