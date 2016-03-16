#   Makefile - for procfs
# 
#   Copyright (C) 2008 Free Software Foundation, Inc.
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

dir := procfs
makemode := server

target = procfs

SRCS = procfs.c netfs.c procfs_dir.c process.c proclist.c rootdir.c dircat.c main.c mach_debugUser.c default_pagerUser.c
LCLHDRS = dircat.h main.h process.h procfs.h procfs_dir.h proclist.h rootdir.h

OBJS = $(SRCS:.c=.o)
HURDLIBS = netfs fshelp iohelp ps ports ihash shouldbeinlibc
LDLIBS = -lpthread

include ../Makeconf
