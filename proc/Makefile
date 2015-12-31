# Copyright (C) 1992, 1993, 1994, 1995, 1996, 2012 Free Software Foundation,
# Inc.
#
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

target = proc
SRCS = wait.c hash.c host.c info.c main.c mgt.c	notify.c pgrp.c msg.c \
       cpu-types.c stubs.c

MIGSFLAGS = -imacros $(srcdir)/mig-mutate.h

MIGSTUBS = processServer.o notifyServer.o \
	ourmsgUser.o proc_excUser.o proc_excServer.o \
	task_notifyServer.o
OBJS = $(SRCS:.c=.o) $(MIGSTUBS)
HURDLIBS = ihash ports shouldbeinlibc
LDLIBS = -lpthread

include ../Makeconf

mutated_ourmsg_U.h: ourmsg_U.h
	sed -e 's/_msg_user_/_ourmsg_user_/' < $< > $@
