# Makefile for ext2fs
#
#   Copyright (C) 1994, 1995 Free Software Foundation, Inc.
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

dir := ext2fs
makemode := server

SRCS = balloc.c bitmap.c devio.c dir.c ext2fs.c getblk.c hyper.c ialloc.c \
       inode.c pager.c pokel.c truncate.c
OBJS = $(SRCS:.c=.o) error.o
LCLHDRS = ext2fs.h ext2_fs.h ext2_fs_i.h
REMHDRS = ../hurd/diskfs.h ../hurd/ports.h ../hurd/pager.h\
		../hurd/ioserver.h ../hurd/fshelp.h
HURDLIBS = libdiskfs libports libpager libioserver libfshelp libthreads \
	libihash
target = ext2fs

CPPFLAGS += -I../lib
CPPFLAGS += $(CPPFLAGS-$(notdir $<))
vpath %.c ../lib

include ../Makeconf

$(OBJS): ext2fs.h ext2_fs.h ext2_fs_i.h
$(OBJS): $(REMHDRS)

CPPFLAGS-error.c = -Dprogram_name=program_invocation_name -DHAVE_VPRINTF -DSTDC_HEADERS -DHAVE_STRERROR
