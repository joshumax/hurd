# Makefile for storeio
#
#   Copyright (C) 1995, 1996, 1997, 2000, 2012, 2026 Free Software
#   Foundation, Inc.
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

dir := storeio
makemode := server

#CFLAGS += -DDEBUG

target = storeio storeio.static
SRCS = dev.c storeio.c pager.c $(and $(PARTED_LIBS),part.c)

OBJS = $(SRCS:.c=.o)
HURDLIBS = netfs pager fshelp iohelp store ports ihash shouldbeinlibc
LDLIBS = -lpthread $(PARTED_LIBS) $(and $(HAVE_LIBBZ2),-lbz2) $(and $(HAVE_LIBZ),-lz)

include ../Makeconf

storeio.static: $(boot-store-types:%=../libstore/libstore_%.a)
