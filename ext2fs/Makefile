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

SRCS alloc.c consts.c devio.c dir.c hyper.c inode.c main.c pager.c \
       sizes.c subr.c tables.c bmap.c pokeloc.c
OBJS = $(SRCS:.c=.o) exec_server_image.o
LCLHDRS = ext2fs.h fs.h ext2_fs.h ext2_fs_sb.h ext2_fs_i.h
DIST_FILES = BROKEN
REMHDRS = ../hurd/diskfs.h ../hurd/ports.h ../hurd/pager.h\
		../hurd/ioserver.h ../hurd/fshelp.h
#HURDLIBS = libdiskfs libports libdiskfs libpager libioserver \
#	libfshelp libdiskfs libthreads libports
HURDLIBS = libdiskfs libports libpager libioserver libfshelp libthreads
target = ext2fs

include ../Makeconf

exec_server_image.o: ../exec/exec ../mkbootfs/mkbootfs
	rsh $(mighost) -n cd `pwd` \; \
	../mkbootfs/mkbootfs ../exec/exec exec_server_image.o

$(OBJS): ext2fs.h 
$(OBJS): $(REMHDRS)

$(foreach dir,mkbootfs exec,../$(dir)/%): FORCE
	$(MAKE) -C $(@D) $(@F)
