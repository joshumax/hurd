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

OBJS = wait.o hash.o host.o info.o main.o mgt.o	notify.o pgrp.o msg.o \
	cpu-types.o stubs.o processServer.o notifyServer.o \
	process_replyUser.o ourmsgUser.o interruptServer.o proc_excUser.o \
	proc_excServer.o proc_excreplUser.o proc_excreplServer.o

SRCS = wait.c hash.c host.c info.c main.c mgt.c	notify.c pgrp.c msg.c \
       cpu-types.c stubs.c

LCLHDRS = proc.h
REMHDRS = ../libthreads/cthreads.h ../libihash/ihash.h
HURDLIBS = libthreads libihash
DIST_FILES = proc_exc.defs proc_excrepl.defs
target = proc

include ../Makeconf

# The reason for -Dout= is to prevent errors for get_init_port,
# get_init_ports, get_init_int, get_init_ints, get_dtable, and get_fd.
# We don't use those, so we're safe in breaking them.
ourmsg_U.h ourmsgUser.c: ourmsg.defs $(includedir)/hurd/hurd_types.defs
	$(CPP) $(CPPFLAGS) -Droutine=simpleroutine -Dout= ourmsg.defs \
	| $(MIGCOM) -prefix nowait_ -server /dev/null -user ourmsgUser.c -header ourmsg_U.h

ourmsg.defs: $(includedir)/hurd/msg.defs
	rm -f $@
	ln -s $< $@
