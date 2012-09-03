# Makefile for ufs
#
#   Copyright (C) 1994,95,96,99,2000,02,12 Free Software Foundation, Inc.
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

target = ufs
SRCS = alloc.c consts.c dir.c hyper.c inode.c main.c pager.c \
       sizes.c subr.c tables.c bmap.c pokeloc.c xinl.c

OBJS = $(SRCS:.c=.o)
HURDLIBS = diskfs iohelp fshelp store pager ports ihash shouldbeinlibc
OTHERLIBS = -lpthread

include ../Makeconf

ufs.static: $(boot-store-types:%=../libstore/libstore_%.a)
