# 
#   Copyright (C) 1994, 1995 Free Software Foundation
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
OBJS = $(subst .c,.o,$(SRCS)) fsysServer.o error.o
targets = symlink ifsock magic null

CPPFLAGS += -I../lib
CPPFLAGS += $(CPPFLAGS-$(notdir $<))
vpath %.c ../lib
null-HURDLIBS=libtrivfs libports libihash
ifsock-HURDLIBS=libtrivfs libports libihash

include ../Makeconf

symlink: symlink.o fsysServer.o

magic: magic.o fsysServer.o error.o

# The .a files have to be mentioned explicitly due to a make deficiency,
# even though they are already specified by null-HURDLIBS above.
null: null.o error.o ../libtrivfs/libtrivfs.a ../libports/libports.a \
	../libihash/libihash.a

ifsock: ifsock.o error.o ../libtrivfs/libtrivfs.a ../libports/libports.a \
	../libihash/libihash.a ifsockServer.o

CPPFLAGS-error.c = -Dprogram_name=program_invocation_name -DHAVE_VPRINTF -DSTDC_HEADERS -DHAVE_STRERROR
