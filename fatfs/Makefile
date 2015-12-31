#   Copyright (C) 1997, 2003, 2007, 2012 Free Software Foundation
#   Modified by Marcus Brinkmann, 2000-05-05
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

dir := fatfs
makemode := server

target = fatfs
SRCS = inode.c main.c dir.c pager.c fat.c virt-inode.c node-create.c

OBJS = $(SRCS:.c=.o)
HURDLIBS = diskfs iohelp fshelp store pager ports ihash shouldbeinlibc
LDLIBS = -lpthread $(and $(HAVE_LIBBZ2),-lbz2) $(and $(HAVE_LIBZ),-lz)

include ../Makeconf

fatfs.static: $(boot-store-types:%=../libstore/libstore_%.a)
