
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

dir := ufs
makemode := server

SRCS = alloc.c consts.c devio.c dir.c hyper.c inode.c main.c pager.c \
       sizes.c subr.c tables.c bmap.c pokeloc.c
OBJS = $(SRCS:.c=.o)
LCLHDRS = ufs.h fs.h dinode.h dir.h
REMHDRS = ../hurd/diskfs.h ../hurd/ports.h ../hurd/pager.h\
		../hurd/ioserver.h ../hurd/fshelp.h
#HURDLIBS = libdiskfs libports libdiskfs libpager libioserver \
#	libfshelp libdiskfs libthreads libports
HURDLIBS = libdiskfs libports libpager libioserver libfshelp libthreads libihash
LDFLAGS = -Wl,--no-keep-memory
target = ufs

CPPFLAGS += -I../lib
CPPFLAGS += $(CPPFLAGS-$(notdir $<))
vpath %.c ../lib

include ../Makeconf

