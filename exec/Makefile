# Copyright (C) 1993,94,95,96,98,99,2000,01,02,10,12 Free Software Foundation,
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

dir := exec
makemode := server

SRCS = exec.c main.c hashexec.c hostarch.c \
       $(gzip-sources) $(bzip2-sources)
OBJS = main.o hostarch.o exec.o hashexec.o \
       execServer.o exec_startupServer.o \
       $(gzip-objects) $(bzip2-objects)
gzip-sources = unzip.c util.c inflate.c
gzip-objects = $(gzip-sources:%.c=%.o)
bzip2-sources = do-bunzip2.c
bzip2-objects = $(bzip2-sources:%.c=%.o)

target = exec
#targets = exec exec.static
#OTHERLIBS = -lbfd -liberty
HURDLIBS = trivfs fshelp iohelp ports ihash shouldbeinlibc
OTHERLIBS = -lpthread

exec-MIGSFLAGS = -imacros $(srcdir)/execmutations.h

include ../Makeconf

CPPFLAGS += -DGZIP -DBZIP2 # -DBFD

exec.static exec: $(OBJS) $(library_deps)
