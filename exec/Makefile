# Copyright (C) 1993, 1994, 1995 Free Software Foundation, Inc.
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

SRCS = exec.c hostarch.c
OBJS = hostarch.o exec.o execServer.o fsysServer.o notifyServer.o \
       $(gzip-objects)
gzip-objects = unzip.o util.o inflate.o
LCLHDRS = gzip.h crypt.h tailor.h
target = exec
DIST_FILES = core.c gcore.c hashexec.c
OTHERLIBS = -lbfd -liberty

include ../Makeconf

vpath lib%.a $(crossdir)/lib

CPPFLAGS += -DGZIP -DBFD

exec.o: exec.c fsys_S.h notify_S.h exec_S.h 
exec.o hostarch.o: a.out.h a.out.gnu.h

unzip.o util.o inflate.o: gzip.h crypt.h tailor.h
