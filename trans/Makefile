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

dir := trans
makemode := servers

SRCS = ifsock.c symlink.c magic.c null.c
targets = symlink ifsock magic null

CPPFLAGS += -I../lib
CPPFLAGS += $(CPPFLAGS-$(notdir $<))
vpath %.c ../lib

include ../Makeconf

symlink: symlink.o fsysServer.o
symlink.o: fsys_S.h

magic: magic.o fsysServer.o error.o
magic.o: fsys_S.h

null: null.o error.o $(libtrivfs) $(libports)
null.o: ../hurd/trivfs.h ../hurd/ports.h

ifsock: ifsock.o error.o $(libtrivfs) $(libports) ifsockServer.o
ifsock.o: ifsock_S.h ../hurd/trivfs.h ../hurd/ports.h

CPPFLAGS-error.c = -Dprogram_name=program_invocation_name -DHAVE_VPRINTF -DSTDC_HEADERS -DHAVE_STRERROR
