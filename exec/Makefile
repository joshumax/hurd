# Copyright (C) 1993, 1994, 1995, 1996 Free Software Foundation, Inc.
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

SRCS = exec.c main.c hashexec.c hostarch.c unzip.c util.c inflate.c
OBJS = main.o hostarch.o exec.o hashexec.o \
       execServer.o exec_startupServer.o \
       $(gzip-objects)
gzip-objects = unzip.o util.o inflate.o
LCLHDRS = gzip.h crypt.h tailor.h priv.h execmutations.h
target = exec
DIST_FILES = core.c gcore.c elfcore.c exectrans.c
#OTHERLIBS = -lbfd -liberty

exec-MIGSFLAGS = -imacros $(srcdir)/execmutations.h

include ../Makeconf

CPPFLAGS += -DGZIP # -DBFD

exec: ../libtrivfs/libtrivfs.so ../libthreads/libthreads.so \
      ../libshouldbeinlibc/libshouldbeinlibc.so ../libfshelp/libfshelp.so
