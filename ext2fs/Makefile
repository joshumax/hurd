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

target = ext2fs
SRCS = balloc.c bitmap.c devio.c dir.c ext2fs.c getblk.c hyper.c ialloc.c \
       inode.c pager.c pokel.c truncate.c
OBJS = $(SRCS:.c=.o)
LCLHDRS = ext2fs.h ext2_fs.h ext2_fs_i.h

ext2fs: $(OBJS) ../libdiskfs/libdiskfs.a ../libports/libports.a ../libpager/libpager.a ../libioserver/libioserver.a ../libfshelp/libfshelp.a ../libthreads/libthreads.a ../libihash/libihash.a ../libshouldbeinlibc/libshouldbeinlibc.a

include ../Makeconf
