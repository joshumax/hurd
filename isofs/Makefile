
#   Copyright (C) 1997, 2000, 2005 Free Software Foundation
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

dir := isofs
makemode := server

target = iso9660fs
SRCS = inode.c main.c lookup.c pager.c rr.c
LCLHDRS = iso9660.h isofs.h rr.h 
DIST_FILES = EXTENSIONS

OBJS = $(SRCS:.c=.o)
HURDLIBS = diskfs iohelp fshelp store pager threads ports ihash shouldbeinlibc

include ../Makeconf

iso9660fs.static: $(boot-store-types:%=../libstore/libstore_%.a)
