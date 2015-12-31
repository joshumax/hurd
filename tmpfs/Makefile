# Makefile for tmpfs
#
#   Copyright (C) 2000,01,02,12 Free Software Foundation, Inc.
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

dir := tmpfs
makemode := server

target = tmpfs
SRCS = tmpfs.c node.c dir.c pager-stubs.c
OBJS = $(SRCS:.c=.o) default_pagerUser.o
# XXX The shared libdiskfs requires libstore even though we don't use it here.
HURDLIBS = diskfs pager iohelp fshelp store ports ihash shouldbeinlibc
LDLIBS = -lpthread

include ../Makeconf
