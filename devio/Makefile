# Makefile for devio
# 
#   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

dir := devio
makemode := server

target = devio
SRCS = dev.c iostate.c window.c devio.c open.c devpager.c io.c rdwr.c mem.c
LCLHDRS = dev.h iostate.h window.h open.h ptypes.h mem.h

OBJS = $(SRCS:.c=.o)

include ../Makeconf

devio: $(OBJS) ../libtrivfs/libtrivfs.a ../libpager/libpager.a ../libports/libports.a ../libfshelp/libfshelp.a ../libthreads/libthreads.a ../libihash/libihash.a ../libshouldbeinlibc/libshouldbeinlibc.a
