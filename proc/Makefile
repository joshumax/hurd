# Copyright (C) 1992, 1993, 1994, 1995 Free Software Foundation, Inc.
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
makemode := server

MIGSFLAGS ="-DPROCESS_INTRAN=pstruct_t reqport_find (process_t)" \
	"-DPROCESS_IMPORTS=import \"proc.h\";" 

OBJS = wait.o hash.o ihash.o host.o info.o main.o mgt.o	notify.o pgrp.o msg.o \
	primes.o cpu-types.o stubs.o \
	processServer.o notifyServer.o process_replyUser.o ourmsgUser.o \
	interruptServer.o proc_excUser.o proc_excServer.o proc_excreplUser.o \
	proc_excreplServer.o

SRCS = wait.c hash.c host.c info.c main.c mgt.c	notify.c pgrp.c msg.c \
       cpu-types.c stubs.c

CPPFLAGS += -I../lib
vpath %.c ../lib

LCLHDRS = proc.h
REMHDRS = ../libthreads/cthreads.h
HURDLIBS = libthreads
DIST_FILES = proc_exc.defs proc_excrepl.defs
target = proc

include ../Makeconf

$(OBJS): proc.h 
wait.o host.o info.o mgt.o pgrp.o: process_S.h
msg.o: process_reply_U.h
notify.o: notify_S.h
pgrp.o: ourmsg_U.h
wait.o: process_reply_U.h ourmsg_U.h interrupt_S.h
mgt.o: proc_exc_S.h proc_exc_U.h proc_excrepl_U.h proc_excrepl_S.h ourmsg_U.h

# The reason for -Dout= is to prevent errors for get_init_port,
# get_init_ports, get_init_int, get_init_ints, get_dtable, and get_fd.
# We don't use those, so we're safe in breaking them.
ourmsg_U.h ourmsgUser.c: $(includedir)/hurd/msg.defs $(includedir)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) -Droutine=simpleroutine -Dout= $(includedir)/hurd/msg.defs \
	| $(MIGCOM) -prefix nowait_ -server /dev/null -user ourmsgUser.c -header ourmsg_U.h


